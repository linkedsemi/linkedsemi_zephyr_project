#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vuart_shell, LOG_LEVEL_INF);

SHELL_UART_DEFINE(vuart_transport);
SHELL_DEFINE(shell_vuart, "vuart:", &vuart_transport,
             CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_SIZE,
             CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_TIMEOUT,
             SHELL_FLAG_OLF_CRLF);

#define VUART_NODE DT_NODELABEL(vuart1)
static const struct device *vuart_dev = DEVICE_DT_GET(VUART_NODE);

static int vuart_shell_init(void)
{
    static const struct shell_backend_config_flags cfg_flags =
        SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

    if (!device_is_ready(vuart_dev)) {
        LOG_ERR("vuart device not ready");
        return -ENODEV;
    }

    int ret = shell_init(&shell_vuart, vuart_dev, cfg_flags, true, LOG_LEVEL_INF);
    if (ret) {
        LOG_ERR("VUART Shell init failed: %d", ret);
    }

    return ret;
}

SYS_INIT(vuart_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
