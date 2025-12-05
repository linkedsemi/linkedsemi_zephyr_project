#include <zephyr/shell/shell.h>

extern int new_delete_test(void);
void print_sys_memory_stats(void);

static int new_delete(const struct shell *shell, size_t argc, char **argv, void *data)
{
    new_delete_test();
    return 0;
}
SHELL_CMD_REGISTER(new_delete, NULL, "new_delete", new_delete);

static int mem_use(const struct shell *shell, size_t argc, char **argv, void *data)
{
    print_sys_memory_stats();
    return 0;
}
SHELL_CMD_REGISTER(mem_use, NULL, "mem_use", mem_use);