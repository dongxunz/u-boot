// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <adc.h>
#include <command.h>
#include <env.h>
#include <log.h>
#include <asm/io.h>
#include <asm/arch-rockchip/boot_mode.h>
#include <dm/device.h>
#include <dm/uclass.h>
#include <linux/printk.h>
#include <mmc.h>

#if (CONFIG_ROCKCHIP_BOOT_MODE_REG == 0)

int setup_boot_mode(void)
{
	return 0;
}

#else

void set_back_to_bootrom_dnl_flag(void)
{
	writel(BOOT_BROM_DOWNLOAD, CONFIG_ROCKCHIP_BOOT_MODE_REG);
}

/*
 * detect download key status by adc, most rockchip
 * based boards use adc sample the download key status,
 * but there are also some use gpio. So it's better to
 * make this a weak function that can be override by
 * some special boards.
 */
#define KEY_DOWN_MIN_VAL	0
#define KEY_DOWN_MAX_VAL	30

__weak int rockchip_dnl_key_pressed(void)
{
#if CONFIG_IS_ENABLED(ADC)
	unsigned int val;
	struct udevice *dev;
	struct uclass *uc;
	int ret;

	ret = uclass_get(UCLASS_ADC, &uc);
	if (ret)
		return false;

	ret = -ENODEV;
	uclass_foreach_dev(dev, uc) {
		if (!strncmp(dev->name, "saradc", 6)) {
			ret = adc_channel_single_shot(dev->name, 0, &val);
			break;
		}
	}

	if (ret == -ENODEV) {
		pr_warn("%s: no saradc device found\n", __func__);
		return false;
	} else if (ret) {
		pr_err("%s: adc_channel_single_shot fail!\n", __func__);
		return false;
	}

	if ((val >= KEY_DOWN_MIN_VAL) && (val <= KEY_DOWN_MAX_VAL))
		return true;
	else
		return false;
#else
	return false;
#endif
}

void rockchip_dnl_mode_check(void)
{
	int ums_to_emmc = 1;
	int emmc_dev = 0; // eMMC dev index is 0
	int ret = 0;
	struct mmc *mmc = NULL;

	if (rockchip_dnl_key_pressed()) {
		printf("download key pressed, entering download mode...\n");

		// printf("\nmaskrom mode...\n");
		// set_back_to_bootrom_dnl_flag();
		// do_reset(NULL, 0, 0, NULL);

		// printf("\nloader mode...\n");
		// run_command("rockusb 0 mmc 0", 0);

		// printf("\nums mode...\n");
		// run_command("ums 0 mmc 0", 0);

		mmc = find_mmc_device(0);
		if (!mmc) {
			printf("boot_mode: no mmc device at slot %x\n", emmc_dev);
			ums_to_emmc = 0;
		} else if (mmc_init(mmc)){
			printf("boot_mode: mmc_init failed at slot %x\n", emmc_dev);
			ums_to_emmc = 0;
		} else if (mmc->bus_width != 8){
			// If the user set eMMC-EN to "ON", it means using NVMe
			printf("boot_mode: mmc[%d]->bus_width: %d\n", emmc_dev, mmc->bus_width);
			ums_to_emmc = 0;
		}
		printf("ums mode: %s\n", ums_to_emmc ? "eMMC" : "NVMe");
		if (ums_to_emmc) {
			ret = run_command("ums 0 mmc 0", 0);
		} else {
			ret = run_command("pci enum; nvme scan; ums 0 nvme 0", 0);
			if (ret) {
				printf("ums NVMe failed: %d, ums to eMMC now!\n", ret);
				ret = run_command("ums 0 mmc 0", 0);
			}
		}
		if (ret) {
			printf("ums failed: %d, goto maskrom mode...\n", ret);
			set_back_to_bootrom_dnl_flag();
			do_reset(NULL, 0, 0, NULL);
		}
	}
}

int setup_boot_mode(void)
{
	void *reg = (void *)CONFIG_ROCKCHIP_BOOT_MODE_REG;
	int boot_mode = readl(reg);

	rockchip_dnl_mode_check();

	boot_mode = readl(reg);
	debug("%s: boot mode 0x%08x\n", __func__, boot_mode);

	/* Clear boot mode */
	writel(BOOT_NORMAL, reg);

	switch (boot_mode) {
	case BOOT_FASTBOOT:
		debug("%s: enter fastboot!\n", __func__);
		env_set("preboot", "setenv preboot; fastboot usb 0");
		break;
	case BOOT_UMS:
		debug("%s: enter UMS!\n", __func__);
		env_set("preboot", "setenv preboot; ums mmc 0");
		break;
	}

	return 0;
}

#endif
