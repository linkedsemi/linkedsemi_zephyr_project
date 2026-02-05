/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_ctx_config);

#define ZEPHYR_PROJECT_USB_VID		0x2fe3

USBD_DEVICE_DEFINE(USB2,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   ZEPHYR_PROJECT_USB_VID, 0x8);

USBD_DEVICE_DEFINE(USB,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc1)),
		   ZEPHYR_PROJECT_USB_VID, 0x9);

USBD_DESC_LANG_DEFINE(usb2_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb2_mfr, "Zephyr Project");
USBD_DESC_PRODUCT_DEFINE(usb2_product, "usb2.0 msc");
USBD_DESC_SERIAL_NUMBER_DEFINE(usb2_sn);

USBD_DESC_LANG_DEFINE(usb1_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb1_mfr, "Zephyr Project");
USBD_DESC_PRODUCT_DEFINE(usb1_product, "usb1.0 msc");
USBD_DESC_SERIAL_NUMBER_DEFINE(usb1_sn);

/* doc string instantiation end */

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "USB Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "USB2 Configuration");

/* doc configuration instantiation start */
static const uint8_t attributes = (IS_ENABLED(CONFIG_SAMPLE_USBD_SELF_POWERED) ?
				   USB_SCD_SELF_POWERED : 0) |
				  (IS_ENABLED(CONFIG_SAMPLE_USBD_REMOTE_WAKEUP) ?
				   USB_SCD_REMOTE_WAKEUP : 0);

/* Full speed configuration */
USBD_CONFIGURATION_DEFINE(usb1_fs_config,
			  attributes,
			  CONFIG_SAMPLE_USBD_MAX_POWER, &fs_cfg_desc);

/* High speed configuration */
USBD_CONFIGURATION_DEFINE(usb2_hs_config,
			  attributes,
			  CONFIG_SAMPLE_USBD_MAX_POWER, &hs_cfg_desc);

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

struct usbd_context *usbd_init_usb2(usbd_msg_cb_t msg_cb)
{
	int err;

	/* doc add string descriptor start */
	err = usbd_add_descriptor(&USB2, &usb2_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB2, &usb2_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB2, &usb2_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB2, &usb2_sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	/* doc add string descriptor end */

	if (usbd_caps_speed(&USB2) == USBD_SPEED_HS) {
		err = usbd_add_configuration(&USB2, USBD_SPEED_HS,
					     &usb2_hs_config);
		if (err) {
			LOG_ERR("Failed to add High-Speed configuration");
			return NULL;
		}
		usbd_register_class(&USB2, "msc_0", USBD_SPEED_HS, 1);
		sample_fix_code_triple(&USB2, USBD_SPEED_HS);
	}

	if (msg_cb != NULL) {
		/* doc device init-and-msg start */
		err = usbd_msg_register_cb(&USB2, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
		/* doc device init-and-msg end */
	}

	/* doc device init start */
	err = usbd_init(&USB2);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}
	/* doc device init end */

	return &USB2;
}

struct usbd_context *usbd_init_usb1(usbd_msg_cb_t msg_cb)
{
	int err;

	/* doc add string descriptor start */
	err = usbd_add_descriptor(&USB, &usb1_lang);
	if (err) {
		LOG_ERR("Failed to initialize language descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB, &usb1_mfr);
	if (err) {
		LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB, &usb1_product);
	if (err) {
		LOG_ERR("Failed to initialize product descriptor (%d)", err);
		return NULL;
	}

	err = usbd_add_descriptor(&USB, &usb1_sn);
	if (err) {
		LOG_ERR("Failed to initialize SN descriptor (%d)", err);
		return NULL;
	}
	/* doc add string descriptor end */

	err = usbd_add_configuration(&USB, USBD_SPEED_FS,
						&usb1_fs_config);
	if (err) {
		LOG_ERR("Failed to add High-Speed configuration");
		return NULL;
	}
	usbd_register_class(&USB, "msc_1", USBD_SPEED_FS, 1);
	sample_fix_code_triple(&USB, USBD_SPEED_FS);

	if (msg_cb != NULL) {
		/* doc device init-and-msg start */
		err = usbd_msg_register_cb(&USB, msg_cb);
		if (err) {
			LOG_ERR("Failed to register message callback");
			return NULL;
		}
		/* doc device init-and-msg end */
	}

	/* doc device init start */
	err = usbd_init(&USB);
	if (err) {
		LOG_ERR("Failed to initialize device support");
		return NULL;
	}
	/* doc device init end */

	return &USB;
}
