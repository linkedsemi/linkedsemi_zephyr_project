/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include "xhci.h"
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#define DWC_USB3_HOST_NUM_U2_ROOT_PORTS_MAX 16
#define DWC_USB3_HOST_NUM_U2_ROOT_PORTS 1
#define DWC_USB3_HOST_NUM_U3_ROOT_PORTS_MAX 16
#define DWC_USB3_HOST_NUM_U3_ROOT_PORTS 1
#define DWC_USB3_DEVICE_NUM_INT_MAX 32
#define DWC_USB3_DEVICE_NUM_INT 1

struct dwc3_gevnt_reg{
    volatile uint32_t ADRLO;        //0x300
    volatile uint32_t ADRHI;        //0x304
    volatile uint32_t SIZ;            //0x308
    volatile uint32_t COUNT;        //0x30c
};

struct dwc3_global_reg {
    volatile uint32_t GSBUSCFG0;        //0x00
    volatile uint32_t GSBUSCFG1;        //0x04
    volatile uint32_t GTXTHRCFG;        //0x08
    volatile uint32_t GRXTHRCFG;        //0x0c
    volatile uint32_t GCTL;                //0x10
    volatile uint32_t GPMSTS;            //0x14
    volatile uint32_t GSTS;                //0x18
    volatile uint32_t GUCTL1;            //0x1c
    volatile uint32_t GSNPSID;            //0x20
    volatile uint32_t GGPIO;            //0x24
    volatile uint32_t GUID;                //0x28
    volatile uint32_t GUCTL;            //0x2c
    volatile uint32_t GBUSERRADDRLO;    //0x30
    volatile uint32_t GBUSERRADDRHI;    //0x34
    volatile uint32_t GPRTBIMAPLO;        //0x38
    volatile uint32_t GPRTBIMAPHI;        //0x3c
    volatile uint32_t GHWPARAMS0;        //0x40
    volatile uint32_t GHWPARAMS1;        //0x44
    volatile uint32_t GHWPARAMS2;        //0x48
    volatile uint32_t GHWPARAMS3;        //0x4c
    volatile uint32_t GHWPARAMS4;        //0x50
    volatile uint32_t GHWPARAMS5;        //0x54
    volatile uint32_t GHWPARAMS6;        //0x58
    volatile uint32_t GHWPARAMS7;        //0x5c
    volatile uint32_t GDBGFIFOSPACE;    //0x60
    volatile uint32_t GDBGLTSSM;        //0x64
    volatile uint32_t GDBGLNMCC;        //0x68
    volatile uint32_t GDBGBMU;            //0x6c
    volatile uint32_t GDBGLSPMUX;        //0x70
    volatile uint32_t GDBGLSP;            //0x74
    volatile uint32_t GDBGEPINFO0;        //0x78
    volatile uint32_t GDBGEPINFO1;        //0x7c
    volatile uint32_t GPRTBIMAP_HSLO;    //0x80
    volatile uint32_t GPRTBIMAP_HSHI;    //0x84
    volatile uint32_t GPRTBIMAP_FSLO;    //0x88
    volatile uint32_t GPRTBIMAP_FSHI;    //0x8c
    volatile uint32_t RESERVED1;
    volatile uint32_t GERRINJCTL_1;        //0x94
    volatile uint32_t GERRINJCTL_2;        //0x98
    volatile uint32_t GUCTL2;            //0x9c
    volatile uint32_t RESERVED[24];
    volatile uint32_t GUSB2PHYCFG[DWC_USB3_HOST_NUM_U2_ROOT_PORTS]; //0x100
    volatile uint32_t RESERVED2[DWC_USB3_HOST_NUM_U2_ROOT_PORTS_MAX-DWC_USB3_HOST_NUM_U2_ROOT_PORTS];
    volatile uint32_t GUSB2I2CCTL[DWC_USB3_HOST_NUM_U2_ROOT_PORTS]; //0x140
    volatile uint32_t RESERVED3[DWC_USB3_HOST_NUM_U2_ROOT_PORTS_MAX-DWC_USB3_HOST_NUM_U2_ROOT_PORTS];
    volatile uint32_t GUSB2PHYACC[DWC_USB3_HOST_NUM_U2_ROOT_PORTS]; //0x180
    volatile uint32_t RESERVED4[DWC_USB3_HOST_NUM_U2_ROOT_PORTS_MAX-DWC_USB3_HOST_NUM_U2_ROOT_PORTS];
    volatile uint32_t GUSB3PIPECTL[DWC_USB3_HOST_NUM_U3_ROOT_PORTS]; //0x1c0
    volatile uint32_t RESERVED5[DWC_USB3_HOST_NUM_U3_ROOT_PORTS_MAX-DWC_USB3_HOST_NUM_U3_ROOT_PORTS];
    volatile uint32_t GTXFIFOSIZ[32];    //0x200
    volatile uint32_t GRXFIFOSIZ[32];    //0x280
    struct dwc3_gevnt_reg GEVNT[DWC_USB3_DEVICE_NUM_INT]; //0x300
    volatile uint32_t RESERVED6[(DWC_USB3_DEVICE_NUM_INT_MAX-DWC_USB3_DEVICE_NUM_INT)*sizeof(struct dwc3_gevnt_reg)/sizeof(uint32_t)];
    volatile uint32_t GHWPARAMS8;        //0x500
    volatile uint32_t RESERVED7[2];
    volatile uint32_t GUCTL3;            //0x50c
    volatile uint32_t GTXFIFOPRIDEV;    //0x510
    volatile uint32_t RESERVED8;
    volatile uint32_t GTXFIFOPRIHST;    //0x518
    volatile uint32_t GRXFIFOPRIHST;    //0x51c
    volatile uint32_t GFIFOPRIDBC;        //0x520
    volatile uint32_t GDMAHLRATIO;        //0x524
    volatile uint32_t RESERVED9[2];
    volatile uint32_t GFLADJ;            //0x530
    volatile uint32_t RESERVED10[3];
    volatile uint32_t GUSB2RHBCTL[DWC_USB3_HOST_NUM_U2_ROOT_PORTS]; //0x540
};

#define DWC3_GLOBALS_REGS_START         (0xc100)
#define USB_BASE                        (0x40050000)
#define LS_CLK_SET(base, n)             (*(volatile uint32_t *)((base) + (n)))

static struct xhci_hcd hcd;


static int alloc_dev(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t slot_id = xhci_alloc_device(&hcd);
    xhci_set_dev_speed(&hcd, slot_id, xhci_get_port_speed(&hcd, 0));
    xhci_cmd_address_device(&hcd, slot_id, 0);
    printk("alloc slot_id = %d.\n", slot_id);
    return 0;
}

static int address_dev(const struct shell *sh, size_t argc, char **argv)
{
    int res = xhci_cmd_address_device(&hcd, atoi(argv[2]), 1);
	if (!res)
    	printk("address slot %d success.\n", atoi(argv[2]));
	else
    	printk("address slot %d fail.\n", atoi(argv[2]));
    return 0;
}

static int get_dev_desc(const struct shell *sh, size_t argc, char **argv)
{
    static __attribute__((aligned(32)))  uint8_t dev_desc[64];
    int xfer_len = 0;
    struct usb_setup_packet setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x6,
        .wValue = 0x100,
        .wIndex = 0x0,
        .wLength = 64,
    };
    xfer_len = xhci_xfer_control(&hcd, atoi(argv[2]), 0, &setup, dev_desc, 64);
    xhci_cache_invalid(dev_desc, 64);

    if (xfer_len >= 0)
    {
        printk("device desc: ");
        for (int i = 0; i < 64 - xfer_len; i++)
        {
            printk("0x%x ", dev_desc[i]);
        }
        printk("\n");
    }
    return 0;
}

static int get_config_desc(const struct shell *sh, size_t argc, char **argv)
{
    static __attribute__((aligned(32)))  uint8_t config_desc[255];
    int xfer_len = 0;

    struct usb_setup_packet setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x6,
        .wValue = 0x200,
        .wIndex = 0x0,
        .wLength = 255,
    };
    xfer_len = xhci_xfer_control(&hcd, atoi(argv[2]), 0, &setup, config_desc, 255);
    xhci_cache_invalid(config_desc, 255);

    if (xfer_len >= 0)
    {
        printk("config desc: ");
        for (int i = 0; i < 255 - xfer_len; i++)
        {
            printk("0x%x ", config_desc[i]);
        }
        printk("\n");
    }
    return 0;
}

static size_t usb_string_desc_inplace(uint8_t *buf, size_t buf_len)
{
    if (!buf || buf_len < 2 || buf[1] != 0x03)
        return 0;

    size_t available_bytes = buf_len - 2;
    size_t str_len = available_bytes / 2;

    for (size_t i = 0; i < str_len; i++)
    {
        buf[i] = buf[2 + i*2];
    }
    buf[str_len] = '\0';

    return str_len;
}

static int get_string_desc(const struct shell *sh, size_t argc, char **argv)
{
    static __attribute__((aligned(32))) uint8_t string_desc[255];
    int xfer_len = 0;

    struct usb_setup_packet setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x6,
        .wValue = 0x300 | atoi(argv[4]),
        .wIndex = 0x409,
        .wLength = 255,
    };
    xfer_len = xhci_xfer_control(&hcd, atoi(argv[2]), 0, &setup, string_desc, 255);
    xhci_cache_invalid(string_desc, 255);

    if (xfer_len >= 0)
    {
        usb_string_desc_inplace(string_desc, 255 - xfer_len);
        printk("String Descriptor %d: %s\n", atoi(argv[4]), string_desc);
    }

    return 0;
}

static int bulk_in(const struct shell *sh, size_t argc, char **argv)
{
    /* Allowed to begin on a byte address boundary, However, user may find other alignments, such as 64-byte or 128-byte
    alignments, to be more efficient and provide better performance */

    static __attribute__((aligned(64))) uint8_t in_buf[128];
    int xfer_len = 0;

    uint8_t ep_addr = 0x80 | atoi(argv[4]);

    printk("endpoint %x, bulk in...\n", ep_addr);

    struct xhci_ep_config ep_config = {
        .ep_addr = ep_addr,
        .ep_interval = 3,
        .ep_mps = 512,
        .ep_type = EP_BULK_IN
    };
    xhci_cmd_add_endpoint(&hcd, atoi(argv[2]), &ep_config);
    xfer_len = xhci_xfer_bulk(&hcd, atoi(argv[2]), ep_addr, in_buf, 128);
    xhci_cache_invalid(in_buf, 128);

    printk("xfer_len = %d.\n", xfer_len);
    if (xfer_len >= 0)
    {
        printk("IN Data: ");
        for (int i = 0; i < 128 - xfer_len; i++)
        {
            printk("0x%x ", in_buf[i]);
        }
        printk("\n");
    }

    return 0;
}

static int bulk_out(const struct shell *sh, size_t argc, char **argv)
{
    char out_buf[64];
    uint8_t ep_addr = atoi(argv[4]);

    printk("endpoint %x, bulk out...\n", ep_addr);
    struct xhci_ep_config ep_config = {
        .ep_addr = ep_addr,
        .ep_interval = 3,
        .ep_mps = 512,
        .ep_type = EP_BULK_OUT
    };

    xhci_cmd_add_endpoint(&hcd, atoi(argv[2]), &ep_config);

    snprintf(out_buf, 64, "%s", "xhci bulk out");
    xhci_xfer_bulk(&hcd, atoi(argv[2]), ep_addr, out_buf, strlen(out_buf));

    return 0;
}

static int int_in(const struct shell *sh, size_t argc, char **argv)
{
    /* Allowed to begin on a byte address boundary, However, user may find other alignments, such as 64-byte or 128-byte
    alignments, to be more efficient and provide better performance */

    static __attribute__((aligned(64))) uint8_t int_buf[64];
    int xfer_len = 0;
    uint8_t ep_addr = 0x80 | atoi(argv[4]);

    printk("endpoint %x, interrupt in...\n", ep_addr);

    struct xhci_ep_config ep_config = {
        .ep_addr = ep_addr,
        .ep_interval = 3,
        .ep_mps = 64,
        .ep_type = EP_INT_IN
    };
    xhci_cmd_add_endpoint(&hcd, atoi(argv[2]), &ep_config);
    xfer_len = xhci_xfer_interrupt(&hcd, atoi(argv[2]), ep_addr, int_buf, 64);
    xhci_cache_invalid(int_buf, 64);

    printk("xfer_len = %d.\n", xfer_len);
    if (xfer_len >= 0)
    {
        printk("IN Data: ");
        for (int i = 0; i < 64 - xfer_len; i++)
        {
            printk("0x%x ", int_buf[i]);
        }
        printk("\n");
    }

    return 0;
}

static int set_config(const struct shell *sh, size_t argc, char **argv)
{
    struct usb_setup_packet setup = {
        .bmRequestType = 0x00,
        .bRequest = 0x9,
        .wValue = 0x01,
        .wIndex = 0x0,
        .wLength = 0x0,
    };

    if (xhci_xfer_control(&hcd, atoi(argv[2]), 0, &setup, NULL, 0) == 0)
        printk("set config success.\n");
    else
        printk("set config fail.\n");

    return 0;
}

static int hub_port_setfeat(const struct shell *sh, size_t argc, char **argv)
{
    struct usb_setup_packet setup = {
        .bmRequestType = 0x23,
        .bRequest = 0x3,
        .wValue = 0x08,
        .wIndex = 0x4,
        .wLength = 0x0,
    };

    if (xhci_xfer_control(&hcd, atoi(argv[2]), 0, &setup, NULL, 0) == 0)
        printk("port set feature success.\n");
    else
        printk("port set feature fail.\n");

    return 0;
}

static int list_device(const struct shell *sh, size_t argc, char **argv)
{
    return 0;
}

static int help(const struct shell *sh, size_t argc, char **argv)
{
    printk("xhci_help\n");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_xhci,
            SHELL_CMD_ARG(alloc_dev, NULL,
                    "Usage:\n"
                    "xhci alloc_dev\n",
                    alloc_dev, 1, 0),
            SHELL_CMD_ARG(addr_dev, NULL,
                    "Usage:\n"
                    "xhci addr_dev -s <slot_id> \n",
                    address_dev, 3, 0),
            SHELL_CMD_ARG(get_dev_desc, NULL,
                    "Usage:\n"
                    "xhci get_dev_desc -s <slot_id>\n",
                    get_dev_desc, 3, 0),
            SHELL_CMD_ARG(get_str_desc, NULL,
                    "Usage:\n"
                    "xhci get_str_desc -s <slot_id> -n <index>\n",
                    get_string_desc, 5, 0),
            SHELL_CMD_ARG(set_config, NULL,
                        "Usage:\n"
                        "xhci set_config -s <slot_id>\n",
                        set_config, 3, 0),
            SHELL_CMD_ARG(get_config, NULL,
                        "Usage:\n"
                        "xhci get_config -s <slot_id>\n",
                        get_config_desc, 3, 0),
            SHELL_CMD_ARG(hub_port_setfeat, NULL,
                        "Usage:\n"
                        "xhci hub_port_setfeat -s <slot_id>\n",
                        hub_port_setfeat, 3, 0),
            SHELL_CMD_ARG(bulk_in, NULL,
                        "Usage:\n"
                        "xhci bulk_in -s <slot_id> -e <endpoint>\n",
                        bulk_in, 5, 0),
            SHELL_CMD_ARG(bulk_out, NULL,
                        "Usage:\n"
                        "xhci bulk_out -s <slot_id> -e <endpoint>\n",
                        bulk_out, 5, 0),
            SHELL_CMD_ARG(int_in, NULL,
                        "Usage:\n"
                        "xhci int_in -s <slot_id> -e <endpoint>\n",
                        int_in, 5, 0),
            SHELL_CMD_ARG(list_device, NULL,
                        "Usage:\n"
                        "xhci list_device\n",
                        list_device, 1, 0),
            SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(xhci, &sub_xhci,
           "xhci command\n",
           help);

static uint8_t dwc3_phy_cfg_read(uint8_t reg_addr)
{
    uint32_t res;
    /* rst en */
    *(uint32_t *)0x40058010 |= BIT(25);
    k_usleep(1);
    /* set reg_addr and write_data*/
    *(uint32_t *)0x40058010 &= ~(0xff << 2);
    *(uint32_t *)0x40058010 |= (reg_addr << 2);
    k_usleep(1);
    /* enable read */
    *(uint32_t *)0x40058010 |= (0x1 << 0);
    k_usleep(1);
    *(uint32_t *)0x40058010 &= ~(0x1 << 0);
    k_usleep(1);
    res = *(uint32_t *)0x40058010;
    return (res >> 8) & 0xff;
}

static void dwc3_phy_cfg_write(uint8_t reg_addr, uint8_t write_data)
{
    /* rst en */
    *(uint32_t *)0x40058010 |= BIT(25);
    k_usleep(1);
    /* set reg_addr and write_data*/
    *(uint32_t *)0x40058010 &= ~(0xff << 2);
    *(uint32_t *)0x40058010 |= (reg_addr << 2);
    *(uint32_t *)0x40058010 &= ~(0xff << 16);
    *(uint32_t *)0x40058010 |= (write_data << 16);
    k_usleep(1);
    /* enable write */
    *(uint32_t *)0x40058010 |= (0x1 << 1);
    k_usleep(1);
    *(uint32_t *)0x40058010 &= ~(0x1 << 1);
}

static void udc_dwc3_isr_handler(void *param)
{
    xhci_isr(param);
}

int main(void)
{
    /* clock gate */
    LS_CLK_SET(0x40062000, 0x10) = BIT(0xd);
    /* reset */
    LS_CLK_SET(0x40062000, 0x18) = BIT(0xd);
    LS_CLK_SET(0x40062000, 0x18) = BIT(0xc);
    LS_CLK_SET(0x40062000, 0x10) = BIT(0xc);

    IRQ_CONNECT(29, 3, udc_dwc3_isr_handler, &hcd, 0);
    irq_enable(29);

    struct dwc3_global_reg *dwc3_gbl = (struct dwc3_global_reg *)(USB_BASE + DWC3_GLOBALS_REGS_START);

    /* enable phy pll */
    *(uint32_t *)0x40058014 = 0x131; // pll_en, refclk div, refclk mode (5M or 12M align)
    *(uint32_t *)0x40058004 |= BIT(8); // xhc_bme enable
    *(uint32_t *)0x40058000 |= BIT(5); // Number of USB 2.0 Ports

    dwc3_gbl->GUSB2PHYCFG[0] = 0x40002407;

    uint32_t gctl = dwc3_gbl->GCTL;
    gctl &= ~(0x3 << 12);
    gctl |= 0x1 << 12; // device mode
    dwc3_gbl->GCTL = gctl;

    /* The disconnect detection voltage threshold is set to 490mv */
    uint8_t value = dwc3_phy_cfg_read(0xb);
    value &= ~BIT(6);
    dwc3_phy_cfg_write(0xb, value);

    xhci_init(&hcd, USB_BASE);

    while (1) {
        k_msleep(2000);
    }

    return 0;
}
