#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

/* ---- perf test helpers ---- */

/*
 * DMA-capable buffer alignment. Must be at least CONFIG_SDHC_BUFFER_ALIGNMENT
 * (typically DCACHE_LINE_SIZE=64 when DCACHE is enabled) to avoid the slow
 * bounce-buffer path in sd_ops.c that processes only 1 block per command.
 */
#if CONFIG_SDHC_BUFFER_ALIGNMENT > 1
#define PERF_BUF_ALIGN CONFIG_SDHC_BUFFER_ALIGNMENT
#else
#define PERF_BUF_ALIGN 1
#endif

/* Shell perf test buffer size: 4KB for reasonable throughput measurement */
#define PERF_BUF_SIZE 4096

static const char *speed_units[] = {"B/s", "KiB/s", "MiB/s", "GiB/s"};
static const char *size_units[] = {"B", "KiB", "MiB", "GiB"};
static const uint32_t unit_div = 1024;

static void print_size(const struct shell *sh, double size)
{
	uint8_t idx = 0;

	while (size >= (double)unit_div && idx < ARRAY_SIZE(size_units) - 1) {
		size /= (double)unit_div;
		idx++;
	}

	shell_print(sh, "  Size:      %.1f %s", size, size_units[idx]);
}

static void print_speed(const struct shell *sh, uint64_t elapsed_ms,
			uint32_t loops, double total_bytes)
{
	double bytes_per_loop = total_bytes / (double)loops;
	double throughput = total_bytes;
	uint8_t idx = 0;

	if (elapsed_ms > 0) {
		throughput /= ((double)elapsed_ms / 1000.0);
	}

	while (throughput >= (double)unit_div && idx < ARRAY_SIZE(speed_units) - 1) {
		throughput /= (double)unit_div;
		idx++;
	}

	shell_print(sh, "  Loops:     %u", loops);
	shell_print(sh, "  Total:     %llu ms", elapsed_ms);
	shell_print(sh, "  Per loop:  ~%llu ms",
		    (uint64_t)((double)elapsed_ms / (double)loops));
	shell_print(sh, "  Data/loop: %.1f %s",
		    bytes_per_loop >= unit_div ? bytes_per_loop / unit_div : bytes_per_loop,
		    bytes_per_loop >= unit_div ? size_units[1] : size_units[0]);
	shell_print(sh, "  Throughput: ~%.1f %s", throughput, speed_units[idx]);
}

/* ---- perf_write: fs perf_write <path> <size_kb> [repeat] [buf_size] ---- */
static int cmd_perf_write(const struct shell *sh, size_t argc, char **argv)
{
	struct fs_file_t file;
	uint32_t size_kb;
	uint32_t repeat = 1;
	uint32_t buf_size = PERF_BUF_SIZE;
	uint8_t *buf;
	uint32_t total_bytes;
	uint32_t remaining;
	uint32_t chunk;
	uint64_t start_time, total_time = 0;
	uint32_t loop;
	int err;

	if (argc < 3) {
		shell_error(sh, "Usage: fs perf_write <path> <size_kb> [repeat] [buf_size]");
		return -EINVAL;
	}

	size_kb = strtoul(argv[2], NULL, 0);
	if (size_kb == 0) {
		shell_error(sh, "<size_kb> must be at least 1");
		return -EINVAL;
	}

	if (argc >= 4) {
		repeat = strtoul(argv[3], NULL, 0);
		if (repeat == 0 || repeat > 100) {
			shell_error(sh, "[repeat] must be 1-100");
			return -EINVAL;
		}
	}

	if (argc >= 5) {
		buf_size = strtoul(argv[4], NULL, 0);
		if (buf_size == 0 || buf_size > (256 * 1024)) {
			shell_error(sh, "[buf_size] must be 1-262144");
			return -EINVAL;
		}
	}

	total_bytes = size_kb * 1024;

	buf = k_aligned_alloc(PERF_BUF_ALIGN, buf_size);
	if (!buf) {
		shell_error(sh, "Failed to allocate %u bytes (align=%u)",
			    buf_size, PERF_BUF_ALIGN);
		return -ENOMEM;
	}

	/* Fill buffer with pattern (not random, just repeatable) */
	for (uint32_t i = 0; i < buf_size; i++) {
		buf[i] = (uint8_t)(i & 0xff);
	}

	shell_print(sh, "=== Write Performance Test ===");
	shell_print(sh, "  File:      %s", argv[1]);
	print_size(sh, (double)total_bytes);
	shell_print(sh, "  Buf size:  %u bytes", buf_size);
	shell_print(sh, "  Buf align: %u bytes (DMA: %s)",
		    PERF_BUF_ALIGN,
		    ((uintptr_t)buf & (PERF_BUF_ALIGN - 1)) == 0 ?
		    "fast path" : "slow path");
	shell_print(sh, "  Repeat:    %u", repeat);
	shell_print(sh, "-----------------------------");

	for (loop = 0; loop < repeat; loop++) {
		start_time = k_uptime_get();

		fs_file_t_init(&file);
		err = fs_open(&file, argv[1], FS_O_CREATE | FS_O_WRITE);
		if (err) {
			shell_error(sh, "Failed to open %s (%d)", argv[1], err);
			k_free(buf);
			return -ENOEXEC;
		}

		/* Write data in chunks */
		remaining = total_bytes;
		while (remaining > 0) {
			chunk = MIN(remaining, buf_size);
			err = fs_write(&file, buf, chunk);
			if (err < 0) {
				shell_error(sh, "Write failed at offset %u (%d)",
					    total_bytes - remaining, err);
				fs_close(&file);
				k_free(buf);
				return -ENOEXEC;
			}
			remaining -= err;
		}

		fs_close(&file);

		total_time += k_uptime_delta(&start_time);
		shell_print(sh, "  Loop #%u done.", loop + 1);
	}

	shell_print(sh, "-----------------------------");
	print_speed(sh, total_time, repeat, (double)total_bytes * repeat);
	shell_print(sh, "=============================");

	k_free(buf);
	return 0;
}

/* ---- perf_read: fs perf_read <path> [repeat] [buf_size] ---- */
static int cmd_perf_read(const struct shell *sh, size_t argc, char **argv)
{
	struct fs_dirent dirent;
	struct fs_file_t file;
	uint32_t repeat = 1;
	uint32_t buf_size = PERF_BUF_SIZE;
	uint8_t *buf;
	uint32_t file_size;
	uint32_t total_read;
	uint64_t start_time, total_time = 0;
	uint32_t loop;
	int err;

	if (argc < 2) {
		shell_error(sh, "Usage: fs perf_read <path> [repeat] [buf_size]");
		return -EINVAL;
	}

	if (argc >= 3) {
		repeat = strtoul(argv[2], NULL, 0);
		if (repeat == 0 || repeat > 100) {
			shell_error(sh, "[repeat] must be 1-100");
			return -EINVAL;
		}
	}

	if (argc >= 4) {
		buf_size = strtoul(argv[3], NULL, 0);
		if (buf_size == 0 || buf_size > (256 * 1024)) {
			shell_error(sh, "[buf_size] must be 1-262144");
			return -EINVAL;
		}
	}

	/* Stat the file to get its size */
	err = fs_stat(argv[1], &dirent);
	if (err != 0) {
		shell_error(sh, "fs_stat(%s) failed: %d", argv[1], err);
		return err;
	}

	if (dirent.type != FS_DIR_ENTRY_FILE) {
		shell_error(sh, "%s is not a file", argv[1]);
		return -ENOENT;
	}

	file_size = dirent.size;

	buf = k_aligned_alloc(PERF_BUF_ALIGN, buf_size);
	if (!buf) {
		shell_error(sh, "Failed to allocate %u bytes (align=%u)",
			    buf_size, PERF_BUF_ALIGN);
		return -ENOMEM;
	}

	shell_print(sh, "=== Read Performance Test ===");
	shell_print(sh, "  File:      %s", argv[1]);
	print_size(sh, (double)file_size);
	shell_print(sh, "  Buf size:  %u bytes", buf_size);
	shell_print(sh, "  Buf align: %u bytes (DMA: %s)",
		    PERF_BUF_ALIGN,
		    ((uintptr_t)buf & (PERF_BUF_ALIGN - 1)) == 0 ?
		    "fast path" : "slow path");
	shell_print(sh, "  Repeat:    %u", repeat);
	shell_print(sh, "-----------------------------");

	for (loop = 0; loop < repeat; loop++) {
		total_read = 0;
		start_time = k_uptime_get();

		fs_file_t_init(&file);
		err = fs_open(&file, argv[1], FS_O_READ);
		if (err) {
			shell_error(sh, "Failed to open %s (%d)", argv[1], err);
			k_free(buf);
			return -ENOEXEC;
		}

		while (1) {
			err = fs_read(&file, buf, buf_size);
			if (err < 0) {
				shell_error(sh, "Read failed (%d)", err);
				fs_close(&file);
				k_free(buf);
				return -ENOEXEC;
			}

			total_read += err;

			if (err < (int)buf_size) {
				/* End of file */
				break;
			}
		}

		fs_close(&file);

		if (total_read != file_size) {
			shell_error(sh, "Short read: expected %u, got %u",
				    file_size, total_read);
			k_free(buf);
			return -EIO;
		}

		total_time += k_uptime_delta(&start_time);
		shell_print(sh, "  Loop #%u done.", loop + 1);
	}

	shell_print(sh, "-----------------------------");
	print_speed(sh, total_time, repeat, (double)file_size * repeat);
	shell_print(sh, "=============================");

	k_free(buf);
	return 0;
}

/* ---- Shell command registration ---- */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fs_perf,
	SHELL_CMD_ARG(write, NULL,
		"Write performance test: fs perf write <path> <size_kb> [repeat] [buf_size]",
		cmd_perf_write, 3, 2),
	SHELL_CMD_ARG(read, NULL,
		"Read performance test: fs perf read <path> [repeat] [buf_size]",
		cmd_perf_read, 2, 2),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fs,
	SHELL_CMD(perf, &sub_fs_perf,
		"File read/write performance tests", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(fstest, &sub_fs, "File system commands", NULL);

/* ---- Existing sample logic ---- */
