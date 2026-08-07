#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>
#include "uart_agent.h"

LOG_MODULE_REGISTER(usb_shell, LOG_LEVEL_INF);
SHELL_UART_DEFINE(cdc_shell);

SHELL_DEFINE(shell_cdc, "usb2:", &cdc_shell,
             CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_SIZE,
             CONFIG_SHELL_BACKEND_SERIAL_LOG_MESSAGE_QUEUE_TIMEOUT,
             SHELL_FLAG_OLF_CRLF);

#define CDC_UART_NODE DT_NODELABEL(cdc_acm_uart0)
static const struct device *cdc_uart_dev = DEVICE_DT_GET(CDC_UART_NODE);

#define AGENT_NODE DT_NODELABEL(uart_agent)
static const struct device *agent_dev = DEVICE_DT_GET(AGENT_NODE);

static struct k_work switch_local_work;
static struct k_work switch_peer_work;
static bool local_shell;

static void switch_local_work_handler(struct k_work *work)
{
    if (local_shell == false) {
        local_shell = true;
        uart_agent_stop(agent_dev);
        shell_backend_uart_resume(&shell_cdc);
        shell_start(&shell_cdc);
    }
}

static void switch_peer_work_handler(struct k_work *work)
{
    /*
     * Prevent a race between shell_stop and the shell thread's
     * state_collect exit path:
     *
     *   shell thread                            workqueue (us)
     *   ────────────                            ──────────────
     *   state_collect in progress...
     *                                           suspend UART
     *                                             (stops new RX from waking
     *                                              the shell thread — no
     *                                              new state_collect can
     *                                              start)
     *                                           sleep  →  yield
     *   ... state_collect finishes ...
     *   ... back to idle (k_poll) ...
     *                                           shell_stop
     *                                             (safe: UART is quiet,
     *                                              in-flight state_collect
     *                                              has drained, nothing
     *                                              will undo INITIALIZED)
     *                                           claim UART for forwarding
     *
     * Order matters: suspend first shuts off the source of new triggers,
     * then sleep drains whatever was already in flight. Only then is
     * shell_stop guaranteed to stick.
     */
    shell_backend_uart_suspend(&shell_cdc);
    k_msleep(10);
    shell_stop(&shell_cdc);
    uart_agent_start(agent_dev);
    local_shell = false;
}

static void on_vuart_mode_set(const struct device *dev, void *user_data)
{
    k_work_submit(&switch_local_work);
}

static int cmd_uart_switch_peer(const struct shell *sh, size_t argc, char **argv)
{
    if (sh != &shell_cdc) {
        return -EINVAL;
    }
    k_work_submit(&switch_peer_work);
    return 0;
}

SHELL_CMD_REGISTER(uart_switch_sec, NULL, "Switch uart to peer core", cmd_uart_switch_peer);

static int usb_shell_init(void)
{
    static const struct shell_backend_config_flags cfg_flags =
        SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

    int ret = shell_init(&shell_cdc, cdc_uart_dev, cfg_flags, true, LOG_LEVEL_INF);
    if (ret) {
        LOG_ERR("CDC Shell init failed: %d", ret);
        return ret;
    }

    if (!device_is_ready(agent_dev)) {
        LOG_ERR("uart_agent device not ready");
        return -ENODEV;
    }

    local_shell = true;
    k_work_init(&switch_local_work, switch_local_work_handler);
    k_work_init(&switch_peer_work, switch_peer_work_handler);
    uart_agent_callback_set(agent_dev, on_vuart_mode_set, NULL);

    return 0;
}

SYS_INIT(usb_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
