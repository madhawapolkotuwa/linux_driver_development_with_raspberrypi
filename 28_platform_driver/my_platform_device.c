/*
 * my_platform_device.c
 *
 * Platform Driver (Introduction)
 *
 * This module manually registers a platform_device. There is no
 * Device Tree involved here on purpose - the goal of this tutorial
 * is to show what the platform bus is doing "under the hood" before
 * we go back to matching devices through Device Tree compatible
 * strings, like we already did in the GPIO and BMP180 tutorials.
 *
 * A platform_device is basically the kernel's way of saying:
 * "this piece of hardware exists, here is its name, and here is
 * some data / resources that belong to it."
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#define DEVICE_NAME "my_platform_device"

/* This is the private data we want to hand over to the driver.
 * Think of it as the "identity card" of our virtual device.
 */
struct my_platform_data {
	const char *label;
	int value;
};

static struct my_platform_data my_pdata = {
	.label = "mpcoding-virtual-device",
	.value = 42,
};

/* This release function is required by the driver core.
 * The kernel calls it once the device's reference count drops to
 * zero, so it knows it is safe to free any related memory.
 * We have nothing to free here, so we just log it.
 */
static void my_platform_device_release(struct device *dev)
{
	pr_info("%s: release called, device removed cleanly\n", DEVICE_NAME);
}

static struct platform_device my_platform_device = {
	.name = "my_platform_dev",   /* must match the driver's name */
	.id   = -1,                  /* -1 means "only one instance" */
	.dev  = {
		.platform_data = &my_pdata,
		.release       = my_platform_device_release,
	},
};

static int __init my_platform_device_init(void)
{
	int ret;

	pr_info("%s: registering platform device\n", DEVICE_NAME);

	ret = platform_device_register(&my_platform_device);
	if (ret) {
		pr_err("%s: failed to register device\n", DEVICE_NAME);
		return ret;
	}

	return 0;
}

static void __exit my_platform_device_exit(void)
{
	platform_device_unregister(&my_platform_device);
	pr_info("%s: platform device unregistered\n", DEVICE_NAME);
}

module_init(my_platform_device_init);
module_exit(my_platform_device_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding LDD");
MODULE_DESCRIPTION("Manually registered platform device (no DT)");