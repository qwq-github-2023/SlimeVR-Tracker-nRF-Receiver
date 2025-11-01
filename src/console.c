/*
	SlimeVR Code is placed under the MIT license
	Copyright (c) 2025 SlimeVR Contributors

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#include "globals.h"
#include "system/system.h"
#include "build_defines.h"

#define USB DT_NODELABEL(usbd)
#if DT_NODE_HAS_STATUS(USB, okay)

#include <zephyr/drivers/gpio.h>
#include <zephyr/console/console.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>
#include "connection/esb.h"

#include <ctype.h>
#include <stdlib.h>

#define DFU_DBL_RESET_MEM 0x20007F7C
#define DFU_DBL_RESET_APP 0x4ee5677e

uint32_t* dbl_reset_mem = ((uint32_t*) DFU_DBL_RESET_MEM);

LOG_MODULE_REGISTER(console, LOG_LEVEL_INF);

static void console_thread(void);
K_THREAD_DEFINE(console_thread_id, 1024, console_thread, NULL, NULL, NULL, 6, 0, 0);

#define DFU_EXISTS CONFIG_BUILD_OUTPUT_UF2 || CONFIG_BOARD_HAS_NRF5_BOOTLOADER
#define ADAFRUIT_BOOTLOADER CONFIG_BUILD_OUTPUT_UF2
#define NRF5_BOOTLOADER CONFIG_BOARD_HAS_NRF5_BOOTLOADER

#if NRF5_BOOTLOADER
static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
#endif

static const char *meows[] = {
	"Mew",
	"Meww",
	"Meow",
	"Meow meow",
	"Mrrrp",
	"Mrrf",
	"Mreow",
	"Mrrrow",
	"Mrrr",
	"Purr",
	"mew",
	"meww",
	"meow",
	"meow meow",
	"mrrrp",
	"mrrf",
	"mreow",
	"mrrrow",
	"mrrr",
	"purr",
};

static const char *meow_punctuations[] = {
	".",
	"?",
	"!",
	"-",
	"~",
	""
};

static const char *meow_suffixes[] = {
	" :3",
	" :3c",
	" ;3",
	" ;3c",
	" x3",
	" x3c",
	" X3",
	" X3c",
	" >:3",
	" >:3c",
	" >;3",
	" >;3c",
	""
};

static void skip_dfu(void)
{
#if DFU_EXISTS // Using Adafruit bootloader
	(*dbl_reset_mem) = DFU_DBL_RESET_APP; // Skip DFU
	ram_range_retain(dbl_reset_mem, sizeof(dbl_reset_mem), true);
#endif
}

static void print_info(void)
{
	printk(CONFIG_USB_DEVICE_MANUFACTURER " " CONFIG_USB_DEVICE_PRODUCT "\n");
	printk(FW_STRING);

	printk("\nBoard: " CONFIG_BOARD "\n");
	printk("SOC: " CONFIG_SOC "\n");
	printk("Target: " CONFIG_BOARD_TARGET "\n");

	printk("\nDevice address: %012llX\n", *(uint64_t *)NRF_FICR->DEVICEADDR & 0xFFFFFFFFFFFF);
}

static void print_uptime(void)
{
	int64_t uptime = k_ticks_to_us_floor64(k_uptime_ticks());

	uint32_t days = uptime / 86400000000;
	uptime %= 86400000000;
	uint8_t hours = uptime / 3600000000;
	uptime %= 3600000000;
	uint8_t minutes = uptime / 60000000;
	uptime %= 60000000;
	uint8_t seconds = uptime / 1000000;
	uptime %= 1000000;
	uint16_t milliseconds = uptime / 1000;
	uint16_t microseconds = uptime %= 1000;

	printk("Uptime: %u.%02u:%02u:%02u.%03u,%03u\n", days, hours, minutes, seconds, milliseconds, microseconds);
}

static void print_meow(void)
{
	int64_t ticks = k_uptime_ticks();

	ticks %= ARRAY_SIZE(meows) * ARRAY_SIZE(meow_punctuations) * ARRAY_SIZE(meow_suffixes); // silly number generator
	uint8_t meow = ticks / (ARRAY_SIZE(meow_punctuations) * ARRAY_SIZE(meow_suffixes));
	ticks %= (ARRAY_SIZE(meow_punctuations) * ARRAY_SIZE(meow_suffixes));
	uint8_t punctuation = ticks / ARRAY_SIZE(meow_suffixes);
	uint8_t suffix = ticks % ARRAY_SIZE(meow_suffixes);

	printk("%s%s%s\n", meows[meow], meow_punctuations[punctuation], meow_suffixes[suffix]);
}

static void print_list(void)
{
	printk("Stored devices:\n");
	for (uint8_t i = 0; i < stored_trackers; i++)
		printk("%012llX\n", stored_tracker_addr[i]);
}

static void print_qversion(void)
{
	printk("SlimeVR Modified By QiWenQWQ(Receiver)\n");
	printk("Copyright © 2025 QiWenQWQ. All rights reserved.\n\
This software and its accompanying documentation are the intellectual property of QiWenQWQ.  \n\
Unauthorized copying, modification, distribution, or use of this software, in whole or in part,  \n\
for any purpose other than that expressly permitted by QiWenQWQ, is strictly prohibited.\n\
This software is licensed, not sold. The licensee is granted the right to use this software  \n\
only within the scope defined by the license agreement. Any violation of this agreement  \n\
may result in legal action.\n\
QiWenQWQ reserves the right to improve, modify, or discontinue the software at any time  \n\
without prior notice.\n\
For licensing, business cooperation, or support, please contact:  \n\
qiwenqwq@outlook.com\n");
	printk("QmolReceiver V1.1.0\n");
}

static void print_qgpio(void)
{
    static const struct device *gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	static const uint8_t gpio0_pins[] = {
		8,   // clk-gpios
		15,  // led-gpios
		22,  // SPI CS
		24,  // SPI MISO
		6,   // SPI MOSI
		8,   // SPI SCK
		29,  // int0-gpios
		31,  // vcc-gpios
	};
    if (!device_is_ready(gpio0_dev)) {
        printk("%s device not ready!\n", "GPIO0");
        return;
    }

    printk("=== %s Status ===\n", "GPIO0");
    for (size_t i = 0; i < sizeof(gpio0_pins)/sizeof(gpio0_pins[0]); i++) {
        int val = gpio_pin_get(gpio0_dev, gpio0_pins[i]);
        if (val < 0)
            printk("Pin %2d: error\n", gpio0_pins[i]);
        else
            printk("Pin %2d: %d\n", gpio0_pins[i], val);
    }
    printk("====================\n");
}

static void console_thread(void)
{
	console_getline_init();
	while (log_data_pending())
		k_usleep(1);
	k_msleep(100);
	printk("*** " CONFIG_USB_DEVICE_MANUFACTURER " " CONFIG_USB_DEVICE_PRODUCT " ***\n");
	printk(FW_STRING);
	printk("Modified By QiWenQWQ\n");
	printk("Ciallo~ (∠・ω< )⌒★\n");
	printk("qgpio                        Get gpio information\n");
	printk("qversion                     Get qversion information\n");
	printk("qwrite <key> <data>          Qwrite data\n");
	printk("qread <key>                  Get qread data\n");
	printk("info                         Get device information\n");
	printk("uptime                       Get device uptime\n");
	printk("list                         Get paired devices\n");
	printk("reboot                       Soft reset the device\n");
	printk("add <address>                Manually add a device\n");
	printk("remove                       Remove last device\n");
	printk("pair                         Enter pairing mode\n");
	printk("exit                         Exit pairing mode\n");
	printk("clear                        Clear stored devices\n");

	uint8_t command_qgpio[] = "qgpio";
	uint8_t command_qversion[] = "qversion";
	uint8_t command_qwrite[] = "qwrite";
	uint8_t command_qread[] = "qread";
	uint8_t command_info[] = "info";
	uint8_t command_uptime[] = "uptime";
	uint8_t command_list[] = "list";
	uint8_t command_reboot[] = "reboot";
	uint8_t command_add[] = "add";
	uint8_t command_remove[] = "remove";
	uint8_t command_pair[] = "pair";
	uint8_t command_exit[] = "exit";
	uint8_t command_clear[] = "clear";

#if DFU_EXISTS
	printk("dfu                          Enter DFU bootloader\n");

	uint8_t command_dfu[] = "dfu";
#endif

	printk("meow                         Meow!\n");

	uint8_t command_meow[] = "meow";

	while (1) {
		uint8_t *line = console_getline();
		// TODO: currently allow up to 4 args
		uint8_t *arg[4] = {NULL};
		uint8_t args = 0;
		for (uint8_t *p = line; *p; ++p)
		{
			if (args < 2) // only care that the first words are matchable
				*p = tolower(*p);
			if (*p == ' ' && args < 4)
			{
				*p = 0;
				p++;
				if (args < 1)
					*p = tolower(*p);
				if (*p)
				{
					arg[args] = p;
					args++;
				}
			}
		}

		if (memcmp(line, command_qgpio, sizeof(command_qgpio)) == 0)
		{
			print_qgpio();
		}
		else if (memcmp(line, command_qversion, sizeof(command_qversion)) == 0)
		{
			print_qversion();
		}
		else if (memcmp(line, command_qwrite, sizeof(command_qwrite)) == 0) 
		{
			if (args != 2)
			{
				printk("Invalid number of arguments\n");
				continue;
			}
			uint8_t qdata_size= sizeof(uint8_t) * strlen(arg[1]) + 1;
			if (qdata_size > 256) {
				printk("NVS memory out\n");
			} else {
				for (uint8_t i = 0; i < qdata_size; ++i){
					arg[1][i] ^= arg[0][i % strlen(arg[0])];
				}

				qwritedown_size = qdata_size;
				sys_write(QWRITEDOWNSIZE_ID, NULL, &qwritedown_size, sizeof(qwritedown_size));

				memcpy(qwritedown_data, arg[1], qdata_size);
				sys_write(QWRITEDOWN_ID, NULL, qwritedown_data, sizeof(qwritedown_data));

				printk("Writedown succed Size: [%u]\n", qdata_size);
			}
		}
		else if (memcmp(line, command_qread, sizeof(command_qread)) == 0) 
		{
			if (args != 1)
			{
				printk("Invalid number of arguments\n");
				continue;
			}

			sys_read(QWRITEDOWNSIZE_ID, &qwritedown_size, sizeof(qwritedown_size));
			sys_read(QWRITEDOWN_ID, qwritedown_data, sizeof(qwritedown_data));

			for (size_t i = 0; i < qwritedown_size; ++i){
				qwritedown_data[i] ^= arg[0][i % strlen(arg[0])];
			}
			printk("%s\nRead Finial Size: [%u];\n", qwritedown_data, qwritedown_size);
		}
		else if (memcmp(line, command_info, sizeof(command_info)) == 0)
		{
			print_info();
		}
		else if (memcmp(line, command_uptime, sizeof(command_uptime)) == 0)
		{
			print_uptime();
		}
		else if (memcmp(line, command_add, sizeof(command_add)) == 0)
		{
			uint64_t addr = strtoull(arg[0], NULL, 16);
			uint8_t buf[13];
			snprintk(buf, 13, "%012llx", addr);
			if (addr != 0 && memcmp(buf, arg[0], 13) == 0)
				esb_add_pair(addr, true);
			else
				printk("Invalid address\n");
		}
		else if (memcmp(line, command_remove, sizeof(command_remove)) == 0)
		{
			esb_pop_pair();
		}
		else if (memcmp(line, command_list, sizeof(command_list)) == 0)
		{
			print_list();
		}
		else if (memcmp(line, command_reboot, sizeof(command_reboot)) == 0)
		{
			skip_dfu();
			sys_reboot(SYS_REBOOT_COLD);
		}
		else if (memcmp(line, command_pair, sizeof(command_pair)) == 0)
		{
			esb_reset_pair();
		}
		else if (memcmp(line, command_exit, sizeof(command_exit)) == 0)
		{
			esb_finish_pair();
		}
		else if (memcmp(line, command_clear, sizeof(command_clear)) == 0) 
		{
			esb_clear();
		}
#if DFU_EXISTS
		else if (memcmp(line, command_dfu, sizeof(command_dfu)) == 0)
		{
#if ADAFRUIT_BOOTLOADER
			NRF_POWER->GPREGRET = 0x57;
			sys_reboot(SYS_REBOOT_COLD);
#endif
#if NRF5_BOOTLOADER
			gpio_pin_configure(gpio_dev, 19, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
#endif
		}
#endif
		else if (memcmp(line, command_meow, sizeof(command_meow)) == 0) 
		{
			print_meow();
		}
		else
		{
			printk("Unknown command\n");
		}
	}
}

#endif