#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    while (true) {
        LOG_INF("app core hello world!!");
        k_msleep(1000);
    }
    return 0;
}
