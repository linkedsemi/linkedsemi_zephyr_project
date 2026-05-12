/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(usb_device_init);

#define UDC_NODE DT_NODELABEL(zephyr_udc0)

USBD_DEVICE_DEFINE(usb2,
		   DEVICE_DT_GET(UDC_NODE),
		   CONFIG_USBD_VID, CONFIG_USBD_PID);
/* doc device instantiation end */

/* doc string instantiation start */
USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(sample_mfr, CONFIG_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(product, CONFIG_USBD_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
/* doc string instantiation end */

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

/* doc configuration instantiation start */
static const uint8_t attributes = (IS_ENABLED(CONFIG_USBD_SELF_POWERED) ?
				   USB_SCD_SELF_POWERED : 0) |
				  (IS_ENABLED(CONFIG_USBD_REMOTE_WAKEUP) ?
				   USB_SCD_REMOTE_WAKEUP : 0);

/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(fs_config,
			  attributes,
			  CONFIG_USBD_MAX_POWER, &fs_cfg_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(hs_config,
			  attributes,
			  CONFIG_USBD_MAX_POWER, &hs_cfg_desc);
/* doc configuration instantiation end */

static void sample_fix_code_triple(struct usbd_context *uds_ctx,
				   const enum usbd_speed speed)
{
	/* Always use class code information from Interface Descriptors */
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
	    IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS)) {
		/*
		 * Class with multiple interfaces have an Interface
		 * Association Descriptor available, use an appropriate triple
		 * to indicate it.
		 */
		usbd_device_set_code_triple(uds_ctx, speed,
					    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
	}
}

static struct usbd_context *usbd_ctx_init(usbd_msg_cb_t msg_cb)
{
	int err;

	/* doc add string descriptor start */
	err = usbd_add_descriptor(&usb2, &lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb2, &sample_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb2, &product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&usb2, &sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	/* doc add string descriptor end */

	if (usbd_caps_speed(&usb2) == USBD_SPEED_HS) {
		err = usbd_add_configuration(&usb2, USBD_SPEED_HS,
					     &hs_config);
		if (err) {
			LOG_ERR("Failed to add High-Speed configuration");
			return NULL;
		}

		err = usbd_register_all_classes(&usb2, USBD_SPEED_HS, 1);
		if (err) {
			LOG_ERR("Failed to add register classes");
			return NULL;
		}

		sample_fix_code_triple(&usb2, USBD_SPEED_HS);
	}

	/* doc configuration register start */
	err = usbd_add_configuration(&usb2, USBD_SPEED_FS,
				     &fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration");
		return NULL;
	}
	/* doc configuration register end */

	/* doc functions register start */
	err = usbd_register_all_classes(&usb2, USBD_SPEED_FS, 1);
	if (err) {
		LOG_ERR("Failed to add register classes");
		return NULL;
	}
	/* doc functions register end */

	sample_fix_code_triple(&usb2, USBD_SPEED_FS);

	if (msg_cb != NULL) {
		/* doc device init-and-msg start */
		err = usbd_msg_register_cb(&usb2, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
		/* doc device init-and-msg end */
	}

	/* doc device init start */
	err = usbd_init(&usb2);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}
	/* doc device init end */

	return &usb2;
}

static inline void print_baudrate(const struct device *dev)
{
	uint32_t baudrate;
	int ret;

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_WRN("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_INF("Baudrate %u", baudrate);
	}
}

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		print_baudrate(msg->dev);
	}
}

static int usb_stack_and_device_init(void)
{
    struct usbd_context *ctx;
    int err;

    ctx = usbd_ctx_init(usbd_msg_cb);
    if (ctx == NULL) {
        LOG_ERR("Failed to initialize USB device");
        return -ENODEV;
    }

    err = usbd_enable(ctx);
    if (err) {
        LOG_ERR("Failed to enable device support");
        return err;
    }

    LOG_DBG("USB device support enabled");

    return 0;
}
SYS_INIT(usb_stack_and_device_init, POST_KERNEL, 60);
