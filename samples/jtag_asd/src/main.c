/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <getopt.h>
#include <stdio.h>
#include "ls_soc_gpio.h"

extern int jtag_test_main(int argc, char** argv);

static int jtag_test(const struct shell *sh, size_t argc, char **argv)
{
    jtag_test_main(argc, argv);
    return 0;
}
SHELL_CMD_ARG_REGISTER(jtag_test, NULL, NULL, jtag_test, 1, 6);

static int jtag_switch(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        printf("Used:\n");
        printf("    %s [cpu/pch]\n", argv[0]);
        return 0;
    }

    if (strcmp("cpu", argv[1]) == 0)
    {
        io_write_pin(PF15, 0);
        io_write_pin(PF02, 1); 
        printf("switch to cpu\n");
    }
    else if (strcmp("pch", argv[1]) == 0)
    {
        io_write_pin(PF15, 1);
        io_write_pin(PF02, 0); 
        printf("switch to pch\n");
    }

    return 0;
}
SHELL_CMD_ARG_REGISTER(jtag_switch, NULL, NULL, jtag_switch, 1, 2);

int main(void)
{
    io_cfg_output(PF02);
    io_cfg_output(PF15);
    // default cpu
    io_write_pin(PF15, 0);
    io_write_pin(PF02, 1); 

    printf("jtag asd test!!\n");
    return 0;
}
 