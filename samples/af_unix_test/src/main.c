/*
 * AF_UNIX large data transfer test
 *
 * Tests stream socket large data handling (8K+) including partial writes,
 * blocking/non-blocking modes, and DGRAM size limits.
 *
 * The pipe buffer is CONFIG_NET_UNIX_BUFFER_SIZE=4096 bytes.
 * For SOCK_STREAM, k_pipe_put with min_xfer=1 allows partial writes
 * when the buffer doesn't have enough space. This test verifies that
 * multi-buffer transfers work correctly.
 *
 * For SOCK_DGRAM, the maximum message size is also CONFIG_NET_UNIX_BUFFER_SIZE
 * since DGRAM doesn't support fragmentation.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define STREAM_PATH "/tmp/af_unix_stream"
#define LARGE_SIZE  10240  /* 10K - tests multi-segment resume (4K+4K+2K) */
#define SMALL_SIZE   64
#define DGRAM_LIMIT 4000   /* Just under 4K buffer limit */

/* Statistics */
static int stream_large_ok = 0;
static int stream_large_fail = 0;
static int stream_partial_ok = 0;

/* ================================================================== */
/* Helpers                                                             */
/* ================================================================== */
static int do_bind_listen(const char *path)
{
	unlink(path);
	int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (lfd < 0) { printk("  socket() failed: %d\n", errno); return -1; }
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("  bind(%s) failed: %d\n", path, errno);
		close(lfd); return -1;
	}
	if (listen(lfd, 5) < 0) {
		printk("  listen() failed: %d\n", errno);
		close(lfd); return -1;
	}
	return lfd;
}

static int do_connect(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { printk("  socket() failed: %d\n", errno); return -1; }
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("  connect(%s) failed: %d\n", path, errno);
		close(fd); return -1;
	}
	return fd;
}

/* Fill buffer with a known pattern */
static void fill_pattern(uint8_t *buf, size_t len, int seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t)((seed + i) & 0xFF);
}

/* Verify buffer matches expected pattern; return number of mismatches */
static int verify_pattern(const uint8_t *buf, size_t len, int seed)
{
	int mismatches = 0;
	for (size_t i = 0; i < len; i++) {
		uint8_t expected = (uint8_t)((seed + i) & 0xFF);
		if (buf[i] != expected) {
			if (mismatches < 5) /* print first 5 errors */
				printk("    MISMATCH at offset %zu: got 0x%02x, expected 0x%02x\n",
				       i, buf[i], expected);
			mismatches++;
		}
	}
	return mismatches;
}

/* ================================================================== */
/* STREAM: blocking large send (8K via send())                        */
/* ================================================================== */
static void stream_reader(void *p1, void *p2, void *p3)
{
	int lfd = do_bind_listen(STREAM_PATH);
	if (lfd < 0) return;

	/* Accept connection from sender */
	int cfd = accept(lfd, NULL, NULL);
	if (cfd < 0) {
		printk("  [Reader] accept failed: %d\n", errno);
		close(lfd); return;
	}
	printk("  [Reader] accepted fd=%d\n", cfd);

	/* Read all data in a loop until EOF or total matches expected */
	uint8_t *rbuf = malloc(LARGE_SIZE);
	if (!rbuf) { printk("  [Reader] malloc failed\n"); close(cfd); close(lfd); return; }

	size_t total_read = 0;
	while (total_read < LARGE_SIZE) {
		ssize_t n = recv(cfd, rbuf + total_read, LARGE_SIZE - total_read, 0);
		if (n <= 0) {
			printk("  [Reader] recv returned %zd (errno=%d) after %zu bytes\n",
			       n, errno, total_read);
			break;
		}
		total_read += n;
		printk("  [Reader] recv %zd bytes (total %zu/%d)\n", n, total_read, LARGE_SIZE);
	}

	/* Verify data integrity */
	int errs = verify_pattern(rbuf, total_read, 0x42);
	printk("  [Reader] received %zu bytes, %d mismatches\n", total_read, errs);

	if (total_read == LARGE_SIZE && errs == 0) {
		printk("  [Reader] *** STREAM large transfer: PASS ***\n");
		stream_large_ok = 1;
	} else {
		printk("  [Reader] *** STREAM large transfer: FAIL ***\n");
		stream_large_fail = 1;
	}

	free(rbuf);
	close(cfd);
	close(lfd);
}

static void stream_sender(void *p1, void *p2, void *p3)
{
	k_sleep(K_MSEC(200));
	int fd = do_connect(STREAM_PATH);
	if (fd < 0) return;

	uint8_t *sbuf = malloc(LARGE_SIZE);
	if (!sbuf) { printk("  [Sender] malloc failed\n"); close(fd); return; }
	fill_pattern(sbuf, LARGE_SIZE, 0x42);

	printk("  [Sender] sending %d bytes via send()...\n", LARGE_SIZE);
	size_t total_sent = 0;
	while (total_sent < LARGE_SIZE) {
		ssize_t n = send(fd, sbuf + total_sent, LARGE_SIZE - total_sent, 0);
		if (n < 0) {
			printk("  [Sender] send error: %d at offset %zu\n", errno, total_sent);
			break;
		}
		total_sent += n;
		printk("  [Sender] sent %zd bytes (total %zu/%d)\n", n, total_sent, LARGE_SIZE);
	}

	printk("  [Sender] total sent: %zu/%d bytes\n", total_sent, LARGE_SIZE);
	free(sbuf);
	close(fd);
}

/* ================================================================== */
/* STREAM: non-blocking large send via sendmsg()                      */
/* ================================================================== */
static void stream_nb_reader(void *p1, void *p2, void *p3)
{
	int lfd = do_bind_listen(STREAM_PATH);
	if (lfd < 0) return;

	int cfd = accept(lfd, NULL, NULL);
	if (cfd < 0) { close(lfd); return; }

	uint8_t *rbuf = malloc(LARGE_SIZE);
	if (!rbuf) { close(cfd); close(lfd); return; }

	size_t total_read = 0;
	while (total_read < LARGE_SIZE) {
		ssize_t n = recv(cfd, rbuf + total_read, LARGE_SIZE - total_read, 0);
		if (n <= 0) break;
		total_read += n;
	}

	int errs = verify_pattern(rbuf, total_read, 0x99);
	printk("  [NB_Reader] received %zu bytes, %d mismatches\n", total_read, errs);
	if (total_read == LARGE_SIZE && errs == 0) {
		printk("  [NB_Reader] *** STREAM nb sendmsg: PASS ***\n");
		stream_partial_ok = 1;
	} else {
		printk("  [NB_Reader] *** STREAM nb sendmsg: FAIL ***\n");
	}

	free(rbuf);
	close(cfd);
	close(lfd);
}

static void stream_nb_sender(void *p1, void *p2, void *p3)
{
	k_sleep(K_MSEC(200));
	int fd = do_connect(STREAM_PATH);
	if (fd < 0) return;

	/* Set non-blocking */
	int fl = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);

	uint8_t *sbuf = malloc(LARGE_SIZE);
	if (!sbuf) { close(fd); return; }
	fill_pattern(sbuf, LARGE_SIZE, 0x99);

	/* Use sendmsg with 2 iovecs: 4K + 4K = 8K */
	struct iovec iov[2];
	iov[0].iov_base = sbuf;
	iov[0].iov_len  = LARGE_SIZE / 2;
	iov[1].iov_base = sbuf + LARGE_SIZE / 2;
	iov[1].iov_len  = LARGE_SIZE - (LARGE_SIZE / 2);

	struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 2 };

	printk("  [NB_Sender] sendmsg %d bytes (non-blocking, 2 iovecs)...\n", LARGE_SIZE);
	ssize_t ret = sendmsg(fd, &msg, 0);
	printk("  [NB_Sender] sendmsg returned %zd (errno=%d)\n", ret, errno);

	/*
	 * non-blocking mode: sendmsg may only write partial data.
	 * This test verifies partial-write + resume behavior.
	 */
	if (ret > 0 && ret < (ssize_t)LARGE_SIZE) {
		printk("  [NB_Sender] partial write: %zd/%d - demonstrating resume\n",
		       ret, LARGE_SIZE);
		/* Switch to blocking and send remainder in a loop */
		fcntl(fd, F_SETFL, fl);  /* restore blocking */
		size_t total = ret;
		while (total < LARGE_SIZE) {
			ssize_t n = send(fd, sbuf + total, LARGE_SIZE - total, 0);
			if (n <= 0) {
				printk("  [NB_Sender] resume error at %zu: %d\n",
				       total, errno);
				break;
			}
			total += n;
			printk("  [NB_Sender] resumed %zd bytes (total %zu/%d)\n",
			       n, total, LARGE_SIZE);
		}
	} else if (ret < 0) {
		printk("  [NB_Sender] sendmsg failed: %d\n", errno);
	}

	free(sbuf);
	close(fd);
}

/* ================================================================== */
/* DGRAM: verify size limits                                          */
/* ================================================================== */
static void dgram_test(void)
{
	printk("\n=== DGRAM size limit test ===\n");

	/* Receiver: bind and wait */
	int rfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (rfd < 0) { printk("  socket(DGRAM) failed: %d\n", errno); return; }
	unlink("/tmp/af_unix_dgram_test");
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, "/tmp/af_unix_dgram_test", sizeof(addr.sun_path) - 1);
	if (bind(rfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printk("  bind(DGRAM) failed: %d\n", errno);
		close(rfd); return;
	}

	int sfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sfd < 0) { printk("  sender socket failed: %d\n", errno);
		close(rfd); return; }

	/* Test 1: send 2K (should work - within buffer) */
	{
		uint8_t buf[2048];
		fill_pattern(buf, sizeof(buf), 0x11);
		ssize_t n = sendto(sfd, buf, sizeof(buf), 0,
				   (struct sockaddr *)&addr, sizeof(addr));
		printk("  DGRAM send 2048 bytes: %zd (errno=%d) - %s\n",
		       n, errno, n > 0 ? "OK" : "UNEXPECTED FAIL");
		uint8_t rbuf[2048];
		n = recvfrom(rfd, rbuf, sizeof(rbuf), 0, NULL, NULL);
		int errs = verify_pattern(rbuf, n > 0 ? n : 0, 0x11);
		printk("  DGRAM recv %zd bytes, %d mismatches\n", n, errs);
	}

	/* Test 2: send > buffer (should fail - exceeds DGRAM_MAX_SIZE=4096) */
	{
		uint8_t *big = malloc(5000);
		if (big) {
			fill_pattern(big, 5000, 0x22);
			ssize_t n = sendto(sfd, big, 5000, 0,
					   (struct sockaddr *)&addr, sizeof(addr));
			printk("  DGRAM send 5000 bytes: %zd (errno=%d) - %s\n",
			       n, errno,
			       n < 0 ? "EXPECTED FAIL" : "UNEXPECTED OK");
			free(big);
		}
	}

	close(sfd);
	close(rfd);
	printk("=== DGRAM size limit test done ===\n");
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */
K_THREAD_STACK_DEFINE(svr_stack, 8192);
K_THREAD_STACK_DEFINE(cli_stack, 8192);
K_THREAD_STACK_DEFINE(nb_svr_stack, 8192);
K_THREAD_STACK_DEFINE(nb_cli_stack, 8192);
static struct k_thread svr_thr, cli_thr, nb_svr_thr, nb_cli_thr;

int main(void)
{
	printk("\n============================================\n");
	printk("AF_UNIX Large Data Transfer Test\n");
	printk("Buffer size: %d bytes\n", CONFIG_NET_UNIX_BUFFER_SIZE);
	printk("Test data: %d bytes (2x buffer)\n", LARGE_SIZE);
	printk("============================================\n");

	/* Test 1: STREAM blocking large send */
	printk("\n--- Test 1: STREAM blocking %d-byte send ---\n", LARGE_SIZE);
	k_thread_create(&svr_thr, svr_stack, K_THREAD_STACK_SIZEOF(svr_stack),
		stream_reader, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&cli_thr, cli_stack, K_THREAD_STACK_SIZEOF(cli_stack),
		stream_sender, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_sleep(K_SECONDS(3));

	/* Test 2: STREAM non-blocking + sendmsg */
	printk("\n--- Test 2: STREAM non-blocking sendmsg (%d bytes) ---\n", LARGE_SIZE);
	k_thread_create(&nb_svr_thr, nb_svr_stack, K_THREAD_STACK_SIZEOF(nb_svr_stack),
		stream_nb_reader, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&nb_cli_thr, nb_cli_stack, K_THREAD_STACK_SIZEOF(nb_cli_stack),
		stream_nb_sender, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_sleep(K_SECONDS(3));

	/* Test 3: DGRAM size limits */
	dgram_test();
	k_sleep(K_MSEC(500));

	/* Summary */
	printk("\n============================================\n");
	printk("SUMMARY:\n");
	printk("  STREAM blocking large transfer: %s\n",
	       stream_large_ok ? "PASS" : "FAIL");
	printk("  STREAM non-blocking sendmsg:   %s\n",
	       stream_partial_ok ? "PASS" : "FAIL");
	printk("============================================\n");
	return 0;
}
