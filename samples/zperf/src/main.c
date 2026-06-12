/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Zperf sample.
 */
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/net/net_config.h>
#include <stdio.h>

#ifdef CONFIG_NET_LOOPBACK_SIMULATE_PACKET_DROP
#include <zephyr/net/loopback.h>
#endif

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
#include <sample_usbd.h>

static struct usbd_context *sample_usbd;

static int enable_usb_device_next(void)
{
	int err;

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		return -ENODEV;
	}

	err = usbd_enable(sample_usbd);
	if (err) {
		return err;
	}

	return 0;
}
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

// #include "csi_rv32_gcc.h"
// #include "core_rv_pmu.h"
// #include "perf_event_list.h"
__no_optimization
int main(void)
{
	// printf("malloc start\n");
	// void *p = malloc(1024);
	// if (p == NULL) {
	// 	printf("malloc error\n");
	// 	return -1;
	// } else {
	// 	printf("malloc success\n");
	// 	printf("p: %p\n", p);
	// }
	// free(p);

	// int hw_id = 3;
	// int event_code = PERF_HARDWARE_CACHE_L1I_RD_ACCESS;
	// __set_MCYCLE(0);
	// csi_pmu_hpmcounter_disable(hw_id);
	// csi_pmu_hpmcounter_disable_interrupt(hw_id);
	// csi_pmu_hpmcounter_write_value(hw_id, 0);
	// csi_pmu_hpmcounter_write_event(hw_id, event_code);
	// csi_pmu_hpmcounter_enable(hw_id);

	// hw_id = 4;
	// event_code = PERF_HARDWARE_CACHE_L1I_RD_MISS;
	// csi_pmu_hpmcounter_disable(hw_id);
	// csi_pmu_hpmcounter_disable_interrupt(hw_id);
	// csi_pmu_hpmcounter_write_value(hw_id, 0);
	// csi_pmu_hpmcounter_write_event(hw_id, event_code);
	// csi_pmu_hpmcounter_enable(hw_id);

    // __set_MCOUNTEREN(0xffffffff);

#if defined(CONFIG_USB_DEVICE_STACK)
	int ret;

	ret = usb_enable(NULL);
	if (ret != 0) {
		printk("usb enable error %d\n", ret);
	}

	(void)net_config_init_app(NULL, "Initializing network");
#endif /* CONFIG_USB_DEVICE_STACK */

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	if (enable_usb_device_next()) {
		return 0;
	}

	(void)net_config_init_app(NULL, "Initializing network");
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

#ifdef CONFIG_NET_LOOPBACK_SIMULATE_PACKET_DROP
	loopback_set_packet_drop_ratio(1);
#endif

#if defined(CONFIG_NET_DHCPV4) && !defined(CONFIG_NET_CONFIG_SETTINGS)
	net_dhcpv4_start(net_if_get_default());
#endif

	return 0;
}
