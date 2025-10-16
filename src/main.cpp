/*
 * Copyright (c) 2025, Cody Gu <gujiaqi@iscas.ac.cn>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <zephyr/sys/printk.h>

#include "mirac_dds_client.h"

int main(void)
{
    MiracDDS dds_client;

    // Initialize and start MiracDDS thread
    if (!dds_client.startThread())
    {
        printk("Failed to start MiracDDS thread\n");
    }
    else
    {
        printk("MiracDDS initialized and thread started successfully\n");
    }

    while (1)
    {
        // printk("Hello from main thread\n");
        k_sleep(K_MSEC(1000));
    };
}
