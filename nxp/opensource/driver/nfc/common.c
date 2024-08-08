/******************************************************************************
 * Copyright (C) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2021 NXP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 ******************************************************************************/
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *****************************************************************************/
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

#include "common.h"
bool secure_peripheral_not_found = true;


int nfc_parse_dt(struct device *dev, struct platform_configs *nfc_configs,
		 uint8_t interface)
{
	int ret;
	struct device_node *np = dev->of_node;
	struct platform_gpio *nfc_gpio = &nfc_configs->gpio;
	struct platform_ldo *ldo = &nfc_configs->ldo;

	if (!np) {
		pr_err("%s: nfc of_node NULL\n", __func__);
		return -EINVAL;
	}

	nfc_gpio->irq = -EINVAL;
	nfc_gpio->dwl_req = -EINVAL;
	nfc_gpio->ven = -EINVAL;
	nfc_gpio->clkreq = -EINVAL;

	/* irq required for i2c based chips only */
	if (interface == PLATFORM_IF_I2C) {
		nfc_gpio->irq = of_get_named_gpio(np, DTS_IRQ_GPIO_STR, 0);
		if ((!gpio_is_valid(nfc_gpio->irq))) {
			pr_err("%s: nfc irq gpio invalid %d\n", __func__,
			       nfc_gpio->irq);
			return -EINVAL;
		}
		pr_info("%s: irq %d\n", __func__, nfc_gpio->irq);
	}
	nfc_gpio->ven = of_get_named_gpio(np, DTS_VEN_GPIO_STR, 0);
	if ((!gpio_is_valid(nfc_gpio->ven))) {
		pr_err("%s: nfc ven gpio invalid %d\n", __func__, nfc_gpio->ven);
		return -EINVAL;
	}
	/* some products like sn220 does not required fw dwl pin */
	nfc_gpio->dwl_req = of_get_named_gpio(np, DTS_FWDN_GPIO_STR, 0);
	/* not returning failure for dwl gpio as it is optional for sn220 */
	if ((!gpio_is_valid(nfc_gpio->dwl_req)))
		pr_warn("%s: nfc dwl_req gpio invalid %d\n", __func__,
			nfc_gpio->dwl_req);
        /* Read clock request gpio configuration if MGPIO configurations are not preasent */
	if (of_property_read_string(np, DTS_CLKSRC_GPIO_STR, &nfc_configs->clk_src_name)) {
		nfc_configs->clk_pin_voting = false;
	        nfc_gpio->clkreq = of_get_named_gpio(np, DTS_CLKREQ_GPIO_STR, 0);
	        if (!gpio_is_valid(nfc_gpio->clkreq)) {
		   dev_err(dev, "clkreq gpio invalid %d\n", nfc_gpio->clkreq);
		   return -EINVAL;
	        }
	}
	else {
		nfc_configs->clk_pin_voting = true;
	}

	/* Read DTS_SZONE_STR to check secure zone support */
	if (of_property_read_string(np, DTS_SZONE_STR, &nfc_configs->szone)) {
		nfc_configs->CNSS_NFC_HW_SECURE_ENABLE = false;
	}else
		nfc_configs->CNSS_NFC_HW_SECURE_ENABLE = true;

	pr_info("%s: irq %d, ven %d, dwl %d, clkreq %d, clk_pin_voting %d \n", __func__, nfc_gpio->irq, nfc_gpio->ven,
		nfc_gpio->dwl_req, nfc_gpio->clkreq, nfc_configs->clk_pin_voting);

	/* optional property */
	ret = of_property_read_u32_array(np, NFC_LDO_VOL_DT_NAME,
			(u32 *) ldo->vdd_levels,
			ARRAY_SIZE(ldo->vdd_levels));
	if (ret) {
		dev_err(dev, "error reading NFC VDDIO min and max value\n");
		// set default as per datasheet
		ldo->vdd_levels[0] = NFC_VDDIO_MIN;
		ldo->vdd_levels[1] = NFC_VDDIO_MAX;
	}

	/* optional property */
	ret = of_property_read_u32(np, NFC_LDO_CUR_DT_NAME, &ldo->max_current);
	if (ret) {
		dev_err(dev, "error reading NFC current value\n");
		// set default as per datasheet
		ldo->max_current = NFC_CURRENT_MAX;
	}

	return 0;
}

void set_valid_gpio(int gpio, int value)
{
	if (gpio_is_valid(gpio)) {
		pr_debug("%s: gpio %d value %d\n", __func__, gpio, value);
		gpio_set_value(gpio, value);
		/* hardware dependent delay */
		usleep_range(NFC_GPIO_SET_WAIT_TIME_US,
			     NFC_GPIO_SET_WAIT_TIME_US + 100);
	}
}

int get_valid_gpio(int gpio)
{
	int value = -EINVAL;

	if (gpio_is_valid(gpio)) {
		value = gpio_get_value(gpio);
		pr_debug("%s: gpio %d value %d\n", __func__, gpio, value);
	}
	return value;
}

void gpio_set_ven(struct nfc_dev *nfc_dev, int value)
{
	struct platform_gpio *nfc_gpio = &nfc_dev->configs.gpio;
	if (gpio_get_value(nfc_gpio->ven) != value) {
		pr_debug("%s: value %d\n", __func__, value);
		if(secure_peripheral_not_found)
		{
			/*secure peripheral feature is not enabled*/
			gpio_set_value(nfc_gpio->ven, value);
		}
		else
		{
			/*secure peripheral feature is enabled*/
			if(!nfc_hw_secure_check())
				gpio_set_value(nfc_gpio->ven, value);
		}
		/* hardware dependent delay */
		usleep_range(NFC_GPIO_SET_WAIT_TIME_US,
				NFC_GPIO_SET_WAIT_TIME_US + 100);
	}
}

int configure_gpio(unsigned int gpio, int flag)
{
	int ret;

	pr_debug("%s: nfc gpio [%d] flag [%01x]\n", __func__, gpio, flag);
	if (gpio_is_valid(gpio)) {
		ret = gpio_request(gpio, "nfc_gpio");
		if (ret) {
			pr_err("%s: unable to request nfc gpio [%d]\n",
			       __func__, gpio);
			return ret;
		}
		/* set direction and value for output pin */
		if (flag & GPIO_OUTPUT) {
			ret = gpio_direction_output(gpio, (GPIO_HIGH & flag));
			pr_debug("%s: nfc o/p gpio %d level %d\n", __func__,
				 gpio, gpio_get_value(gpio));
		} else {
			ret = gpio_direction_input(gpio);
			pr_debug("%s: nfc i/p gpio %d\n", __func__, gpio);
		}

		if (ret) {
			pr_err("%s: unable to set direction for nfc gpio [%d]\n",
			       __func__, gpio);
			gpio_free(gpio);
			return ret;
		}
		/* Consider value as control for input IRQ pin */
		if (flag & GPIO_IRQ) {
			ret = gpio_to_irq(gpio);
			if (ret < 0) {
				pr_err("%s: unable to set irq [%d]\n", __func__,
				       gpio);
				gpio_free(gpio);
				return ret;
			}
			pr_debug("%s: gpio_to_irq successful [%d]\n", __func__,
				 gpio);
			return ret;
		}
	} else {
		pr_err("%s: invalid gpio\n", __func__);
		ret = -EINVAL;
	}
	return ret;
}

void gpio_free_all(struct nfc_dev *nfc_dev)
{
	struct platform_gpio *nfc_gpio = &nfc_dev->configs.gpio;

	if (gpio_is_valid(nfc_gpio->clkreq))
		gpio_free(nfc_gpio->clkreq);

	if (gpio_is_valid(nfc_gpio->dwl_req))
		gpio_free(nfc_gpio->dwl_req);

	if (gpio_is_valid(nfc_gpio->irq))
		gpio_free(nfc_gpio->irq);

	if (gpio_is_valid(nfc_gpio->ven))
		gpio_free(nfc_gpio->ven);
}

void nfc_misc_unregister(struct nfc_dev *nfc_dev, int count)
{
	pr_debug("%s: entry\n", __func__);
	kfree(nfc_dev->kbuf);
	device_destroy(nfc_dev->nfc_class, nfc_dev->devno);
	cdev_del(&nfc_dev->c_dev);
	class_destroy(nfc_dev->nfc_class);
	unregister_chrdev_region(nfc_dev->devno, count);
	if (nfc_dev->ipcl)
		ipc_log_context_destroy(nfc_dev->ipcl);
}

int nfc_misc_register(struct nfc_dev *nfc_dev,
		      const struct file_operations *nfc_fops, int count,
		      char *devname, char *classname)
{
	int ret = 0;

	ret = alloc_chrdev_region(&nfc_dev->devno, 0, count, devname);
	if (ret < 0) {
		pr_err("%s: failed to alloc chrdev region ret %d\n", __func__,
		       ret);
		return ret;
	}
	nfc_dev->nfc_class = class_create(THIS_MODULE, classname);
	if (IS_ERR(nfc_dev->nfc_class)) {
		ret = PTR_ERR(nfc_dev->nfc_class);
		pr_err("%s: failed to register device class ret %d\n", __func__,
		       ret);
		unregister_chrdev_region(nfc_dev->devno, count);
		return ret;
	}
	cdev_init(&nfc_dev->c_dev, nfc_fops);
	ret = cdev_add(&nfc_dev->c_dev, nfc_dev->devno, count);
	if (ret < 0) {
		pr_err("%s: failed to add cdev ret %d\n", __func__, ret);
		class_destroy(nfc_dev->nfc_class);
		unregister_chrdev_region(nfc_dev->devno, count);
		return ret;
	}
	nfc_dev->nfc_device = device_create(nfc_dev->nfc_class, NULL,
					    nfc_dev->devno, nfc_dev, devname);
	if (IS_ERR(nfc_dev->nfc_device)) {
		ret = PTR_ERR(nfc_dev->nfc_device);
		pr_err("%s: failed to create the device ret %d\n", __func__,
		       ret);
		cdev_del(&nfc_dev->c_dev);
		class_destroy(nfc_dev->nfc_class);
		unregister_chrdev_region(nfc_dev->devno, count);
		return ret;
	}
	nfc_dev->ipcl = ipc_log_context_create(NUM_OF_IPC_LOG_PAGES,
						dev_name(nfc_dev->nfc_device), 0);

	nfc_dev->kbuflen = MAX_NCI_BUFFER_SIZE;
	nfc_dev->kbuf = kzalloc(MAX_NCI_BUFFER_SIZE, GFP_KERNEL | GFP_DMA);
	if (!nfc_dev->kbuf) {
		nfc_misc_unregister(nfc_dev, count);
		return -ENOMEM;
	}

	nfc_dev->cold_reset.rsp_pending = false;
	nfc_dev->cold_reset.is_nfc_enabled = false;
	nfc_dev->cold_reset.is_crp_en = false;
	nfc_dev->cold_reset.last_src_ese_prot = ESE_COLD_RESET_ORIGIN_NONE;

	init_waitqueue_head(&nfc_dev->cold_reset.read_wq);

	return 0;
}

/**
 * nfc_ioctl_power_states() - power control
 * @nfc_dev:    nfc device data structure
 * @arg:    mode that we want to move to
 *
 * Device power control. Depending on the arg value, device moves to
 * different states, refer common.h for args
 *
 * Return: -ENOIOCTLCMD if arg is not supported, 0 if Success(or no issue)
 * and error ret code otherwise
 */
static int nfc_ioctl_power_states(struct nfc_dev *nfc_dev, unsigned long arg)
{
	int ret = 0;
	struct platform_gpio *nfc_gpio = &nfc_dev->configs.gpio;

	if (arg == NFC_POWER_OFF) {
		/*
		 * We are attempting a hardware reset so let us disable
		 * interrupts to avoid spurious notifications to upper
		 * layers.
		 */
		nfc_dev->nfc_disable_intr(nfc_dev);
		set_valid_gpio(nfc_gpio->dwl_req, 0);
		gpio_set_ven(nfc_dev, 0);
		nfc_dev->nfc_ven_enabled = false;
	} else if (arg == NFC_POWER_ON) {
		nfc_dev->nfc_enable_intr(nfc_dev);
		set_valid_gpio(nfc_gpio->dwl_req, 0);

		gpio_set_ven(nfc_dev, 1);
		nfc_dev->nfc_ven_enabled = true;
	} else if (arg == NFC_FW_DWL_VEN_TOGGLE) {
		/*
		 * We are switching to download Mode, toggle the enable pin
		 * in order to set the NFCC in the new mode
		 */
		nfc_dev->nfc_disable_intr(nfc_dev);
		set_valid_gpio(nfc_gpio->dwl_req, 1);
		nfc_dev->nfc_state = NFC_STATE_FW_DWL;
		gpio_set_ven(nfc_dev, 0);
		gpio_set_ven(nfc_dev, 1);
		nfc_dev->nfc_enable_intr(nfc_dev);
	} else if (arg == NFC_FW_DWL_HIGH) {
		/*
		 * Setting firmware download gpio to HIGH
		 * before FW download start
		 */
		pr_debug("set fw gpio high\n");
		set_valid_gpio(nfc_gpio->dwl_req, 1);
		nfc_dev->nfc_state = NFC_STATE_FW_DWL;

	} else if (arg == NFC_VEN_FORCED_HARD_RESET) {
		nfc_dev->nfc_disable_intr(nfc_dev);
		gpio_set_ven(nfc_dev, 0);
		gpio_set_ven(nfc_dev, 1);
		nfc_dev->nfc_enable_intr(nfc_dev);
		pr_info("%s VEN forced reset done\n", __func__);

	} else if (arg == NFC_FW_DWL_LOW) {
		/*
		 * Setting firmware download gpio to LOW
		 * FW download finished
		 */
		pr_debug("set fw gpio LOW\n");
		set_valid_gpio(nfc_gpio->dwl_req, 0);
		nfc_dev->nfc_state = NFC_STATE_NCI;

	} else if (arg == NFC_ENABLE) {
		if(nfc_dev->configs.clk_pin_voting) {
			/* Enabling nfc clock */
			ret = nfc_clock_select(nfc_dev);
			if (ret)
				pr_err("%s unable to select clock\n", __func__);
		}
		/* Setting flag true when NFC is enabled */
		nfc_dev->cold_reset.is_nfc_enabled = true;
	} else if (arg == NFC_DISABLE) {
		if(nfc_dev->configs.clk_pin_voting) {
			/* Disabling nfc clock */
			ret = nfc_clock_deselect(nfc_dev);
			if (ret)
				pr_err("%s unable to disable clock\n", __func__);
		}
		/* Setting flag true when NFC is disabled */
		nfc_dev->cold_reset.is_nfc_enabled = false;
	}  else {
		pr_err("%s bad arg %lu\n", __func__, arg);
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
/**
 * nfc_dev_compat_ioctl - used to set or get data from upper layer.
 * @pfile   file node for opened device.
 * @cmd     ioctl type from upper layer.
 * @arg     ioctl arg from upper layer.
 *
 * NFC and ESE Device power control, based on the argument value
 *
 * Return: -ENOIOCTLCMD if arg is not supported
 * 0 if Success(or no issue)
 * 0 or 1 in case of arg is ESE_GET_PWR/ESE_POWER_STATE
 * and error ret code otherwise
 */
long nfc_dev_compat_ioctl(struct file *pfile, unsigned int cmd,
		      unsigned long arg)
{
	int ret = 0;
	arg = (compat_u64)arg;
	pr_debug("%s: cmd = %x arg = %zx\n", __func__, cmd, arg);
	ret = nfc_dev_ioctl(pfile, cmd, arg);
	return ret;
}
#endif

/**
 * nfc_post_init() - Configuraing Ven GPIO and hardware check
 * @nfc_dev:    nfc device data structure
 *
 * Configure GPIOs post notification from TZ, ensuring it's a non-secure zone.
 *
 * Return: 0 if Success(or no issue) and error ret code otherwise
 */
int nfc_post_init(struct nfc_dev *nfc_dev)
{
	int ret=0;
	static int post_init_success;
	struct platform_configs nfc_configs;
	struct platform_gpio *nfc_gpio;

	if(post_init_success)
		return 0;
	if (!nfc_dev)
		return -ENODEV;

	memcpy(&nfc_configs, &nfc_dev->configs, sizeof(struct platform_configs));
	nfc_gpio = &nfc_configs.gpio;

	ret = configure_gpio(nfc_gpio->ven, GPIO_OUTPUT);
	if (ret) {
		pr_err("%s: unable to request nfc reset gpio [%d]\n",
			__func__, nfc_gpio->ven);
		return ret;
	}

	ret = configure_gpio(nfc_gpio->dwl_req, GPIO_OUTPUT);
	if (ret) {
		pr_err("%s: unable to request nfc firm downl gpio [%d]\n",
			__func__, nfc_gpio->dwl_req);
	}

        if(!(nfc_configs.clk_pin_voting)){
                ret = configure_gpio(nfc_gpio->clkreq, GPIO_INPUT);
                if (ret) {
                        pr_err("%s: unable to request nfc clkreq gpio [%d]\n",
                              __func__, nfc_gpio->clkreq);
                        return ret;
                }
        }

	ret = nfcc_hw_check(nfc_dev);
	if (ret || nfc_dev->nfc_state == NFC_STATE_UNKNOWN) {
		pr_err("nfc hw check failed ret %d\n", ret);
		gpio_free(nfc_gpio->dwl_req);
		gpio_free(nfc_gpio->ven);
		return ret;
	}

	/*Initialising sempahore to disbale NFC Ven GPIO only after eSE is power off flag is set */
	if (nfc_dev->configs.CNSS_NFC_HW_SECURE_ENABLE == true) {
		sema_init(&sem_eSE_pwr_off,0);
	}

	post_init_success = 1;
	pr_info("%s success\n", __func__);
	return 0;


}

/**
 * nfc_hw_secure_check() - Checks the NFC secure zone status
 *
 * Queries the TZ secure libraries if NFC is in secure zone statue or not.
 *
 * Return: 0 if FEATURE_NOT_SUPPORTED or PERIPHERAL_NOT_FOUND or nfc_sec_state = 2(non-secure zone) and
 *  return 1 if nfc_sec_state = 1(secure zone) or error otherwise
 */

bool nfc_hw_secure_check(void)
{
	struct Object client_env;
	struct Object app_object;
	u32 nfc_uid = HW_NFC_UID;
	union ObjectArg obj_arg[2] = {{{0, 0}}};
	int ret;
	bool retstat = 1;
	u8 nfc_sec_state = 0;
	/* get rootObj */
	ret = get_client_env_object(&client_env);
	if (ret) {
		pr_err("Failed to get client_env_object, ret: %d\n", ret);
		return retstat;
	}

	ret = IClientEnv_open(client_env, HW_STATE_UID, &app_object);
	if (ret) {
		pr_debug("Failed to get app_object, ret: %d\n",  ret);
		if (ret == FEATURE_NOT_SUPPORTED) {
			retstat = 0; /* Do not Assert */
			pr_debug("Secure HW feature not supported\n");
		}
		goto exit_release_clientenv;
	}

	obj_arg[0].b = (struct ObjectBuf) {&nfc_uid, sizeof(u32)};
	obj_arg[1].b = (struct ObjectBuf) {&nfc_sec_state, sizeof(u8)};
	ret = Object_invoke(app_object, HW_OP_GET_STATE, obj_arg,
			ObjectCounts_pack(1, 1, 0, 0));

	pr_info("TZ ret: %d nfc_sec_state: %d\n", ret, nfc_sec_state);
	if (ret) {
		if (ret == PERIPHERAL_NOT_FOUND) {
			retstat = 0; /* Do not Assert */
			pr_debug("Secure HW mode is not updated. Peripheral not found\n");
		}
		goto exit_release_app_obj;
	}

	secure_peripheral_not_found = false;

	/* Refer peripheral state utilities for different states of NFC peripherals;
	 * path: vendor/qcom/proprietary/securemsm/peripheralStateUtils/inc/peripheralStateUtils.h
	 */
	if (nfc_sec_state == 1) {
		/*Secure Zone*/
		retstat = 1;
	} else {
		/*Non-Secure Zone*/
		retstat = 0;
	}

	exit_release_app_obj:
		Object_release(app_object);
	exit_release_clientenv:
		Object_release(client_env);

	return  retstat;
}

/**
 * nfc_dynamic_protection_ioctl() - dynamic protection control
 * @nfc_dev:    nfc device data structure
 * @sec_zone_trans:    mode that we want to move to
 * If sec_zone_trans = 1; transition from non-secure zone to secure zone
 * If sec_zone_trans = 0; transition from secure zone to non - secure zone
 *
 * nfc  periheral  dynamic protection control. Depending on the sec_zone_trans value, device moves to
 * secure zone and non-secure  zone
 *
 * Return: -ENOIOCTLCMD if sec_zone_trans val is not supported, 0 if Success(or no issue)
 * and error ret code otherwise
 */
int nfc_dynamic_protection_ioctl(struct nfc_dev *nfc_dev, unsigned long sec_zone_trans)
{
	int ret = 0;

	static int init_flag=1;

	struct platform_gpio *nfc_gpio = &nfc_dev->configs.gpio;
	if(sec_zone_trans == 1) {
		/*check NFC is disabled, only then set Ven GPIO low*/
		if(nfc_dev->cold_reset.is_nfc_enabled == false) {
			pr_debug("%s: value %d\n", __func__, gpio_get_value(nfc_gpio->ven));

			chk_eSE_pwr_off = 1;
			/*check if eSE is active, if yes, wait max of 1sec, until it's inactive  */
			if(nfc_dev->is_ese_session_active == true) {
				if(down_timeout(&sem_eSE_pwr_off, msecs_to_jiffies(1000))) {
					/*waited for 1sec yet eSE not turned off, so, ignoring eSE power off*/
					pr_info("Forcefull shutdown of eSE\n");
				}
			}
			ret = nfc_ioctl_power_states(nfc_dev, 0);
			/*set driver as secure zone, such that no ioctl calls are allowed*/
			nfc_dev->secure_zone = true;
			pr_info("Driver Secure flag set successful\n");
		} else {
			ret = -1;
		}
	}
	else if(sec_zone_trans == 0) {
		chk_eSE_pwr_off = 0;
		nfc_dev->secure_zone = false;

		if(init_flag) {
			/*Initialize once,only during the  first non-secure entry*/
			ret = nfc_post_init(nfc_dev);
			if(ret == 0)
				init_flag=0;
		}
		else {
			if(!gpio_get_value(nfc_gpio->ven))
				ret = nfc_ioctl_power_states(nfc_dev, 1);
		}
		pr_info("Func Driver Secure flag clear successful\n");
	} else {
		pr_info("INVALID ARG\n");
		ret = -ENOIOCTLCMD;
	}

	return ret;
}


/**
 * nfc_dev_ioctl - used to set or get data from upper layer.
 * @pfile   file node for opened device.
 * @cmd     ioctl type from upper layer.
 * @arg     ioctl arg from upper layer.
 *
 * NFC and ESE Device power control, based on the argument value
 *
 * Return: -ENOIOCTLCMD if arg is not supported
 * 0 if Success(or no issue)
 * 0 or 1 in case of arg is ESE_GET_PWR/ESE_POWER_STATE
 * and error ret code otherwise
 */
long nfc_dev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct nfc_dev *nfc_dev = pfile->private_data;

	if (!nfc_dev)
		return -ENODEV;
	if( nfc_dev->configs.CNSS_NFC_HW_SECURE_ENABLE == true) {
	    /*Avoiding ioctl call in secure zone*/
	    if(nfc_dev->secure_zone) {
		    if(cmd!=NFC_SECURE_ZONE) {
			   pr_debug("nfc_dev_ioctl failed\n");
			   return -1;
		    }
	    }
	}

	pr_debug("%s: cmd = %x arg = %zx\n", __func__, cmd, arg);
	switch (cmd) {
	case NFC_SET_PWR:
		ret = nfc_ioctl_power_states(nfc_dev, arg);
		break;
	case ESE_SET_PWR:
		ret = nfc_ese_pwr(nfc_dev, arg);
		break;
	case ESE_GET_PWR:
		ret = nfc_ese_pwr(nfc_dev, ESE_POWER_STATE);
		break;
	case NFCC_GET_INFO:
		ret = nfc_ioctl_nfcc_info(pfile, arg);
		break;
	case ESE_COLD_RESET:
		pr_debug("nfc ese cold reset ioctl\n");
		ret = ese_cold_reset_ioctl(nfc_dev, arg);
		break;
	case NFC_SECURE_ZONE:
		if( nfc_dev->configs.CNSS_NFC_HW_SECURE_ENABLE == true) {
		  ret = nfc_dynamic_protection_ioctl(nfc_dev, arg);
		}
		break;
	default:
		pr_err("%s: bad cmd %lu\n", __func__, arg);
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

int nfc_dev_open(struct inode *inode, struct file *filp)
{
	struct nfc_dev *nfc_dev = NULL;
	nfc_dev = container_of(inode->i_cdev, struct nfc_dev, c_dev);

	if (!nfc_dev)
		return -ENODEV;

	pr_debug("%s: %d, %d\n", __func__, imajor(inode), iminor(inode));

	/* Set flag to block freezer fake signal if not set already.
	 * Without this Signal being set, Driver is trying to do a read
	 * which is causing the delay in moving to Hibernate Mode.
	 */
	if (!(current->flags & PF_NOFREEZE)) {
		current->flags |= PF_NOFREEZE;
		pr_debug("%s: current->flags 0x%x. \n", __func__, current->flags);
	}

	mutex_lock(&nfc_dev->dev_ref_mutex);

	filp->private_data = nfc_dev;

	if (nfc_dev->dev_ref_count == 0) {
		set_valid_gpio(nfc_dev->configs.gpio.dwl_req, 0);

		nfc_dev->nfc_enable_intr(nfc_dev);
	}
	nfc_dev->dev_ref_count = nfc_dev->dev_ref_count + 1;
	mutex_unlock(&nfc_dev->dev_ref_mutex);
	return 0;
}

int nfc_dev_flush(struct file *pfile, fl_owner_t id)
{
	struct nfc_dev *nfc_dev = pfile->private_data;

	if (!nfc_dev)
		return -ENODEV;
	/*
	 * release blocked user thread waiting for pending read during close
	 */
	if (!mutex_trylock(&nfc_dev->read_mutex)) {
		nfc_dev->release_read = true;
		nfc_dev->nfc_disable_intr(nfc_dev);
		wake_up(&nfc_dev->read_wq);
		pr_debug("%s: waiting for release of blocked read\n", __func__);
		mutex_lock(&nfc_dev->read_mutex);
		nfc_dev->release_read = false;
	} else {
		pr_debug("%s: read thread already released\n", __func__);
	}
	mutex_unlock(&nfc_dev->read_mutex);
	return 0;
}

int nfc_dev_close(struct inode *inode, struct file *filp)
{
	struct nfc_dev *nfc_dev = NULL;
	nfc_dev = container_of(inode->i_cdev, struct nfc_dev, c_dev);

	if (!nfc_dev)
		return -ENODEV;

	pr_debug("%s: %d, %d\n", __func__, imajor(inode), iminor(inode));

	/* unset the flag to restore to previous state */
	if (current->flags & PF_NOFREEZE) {
		current->flags &= ~PF_NOFREEZE;
		pr_debug("%s: current->flags 0x%x. \n", __func__, current->flags);
	}

	mutex_lock(&nfc_dev->dev_ref_mutex);
	if (nfc_dev->dev_ref_count == 1) {
		nfc_dev->nfc_disable_intr(nfc_dev);
		set_valid_gpio(nfc_dev->configs.gpio.dwl_req, 0);
	}
	if (nfc_dev->dev_ref_count > 0)
		nfc_dev->dev_ref_count = nfc_dev->dev_ref_count - 1;

	filp->private_data = NULL;

	mutex_unlock(&nfc_dev->dev_ref_mutex);
	return 0;
}

int validate_nfc_state_nci(struct nfc_dev *nfc_dev)
{
	struct platform_gpio *nfc_gpio = &nfc_dev->configs.gpio;
	if(!nfc_dev->secure_zone) {
		if (!gpio_get_value(nfc_gpio->ven)) {
			pr_err("%s: ven low - nfcc powered off\n", __func__);
			return -ENODEV;
		}
	}
	if (get_valid_gpio(nfc_gpio->dwl_req) == 1) {
		pr_err("%s: fw download in-progress\n", __func__);
		return -EBUSY;
	}
	if (nfc_dev->nfc_state != NFC_STATE_NCI) {
		pr_err("%s: fw download state\n", __func__);
		return -EBUSY;
	}
	return 0;
}
