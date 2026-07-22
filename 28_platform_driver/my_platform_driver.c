/*
 * my_platform_driver.c
 *
 * Platform Driver (Introduction)
 *
 * This is the driver half of the pair. It registers itself on the
 * platform bus and waits for a matching platform_device to show up.
 * Matching here is done the classic way, by comparing the device
 * name against platform_driver.driver.name - no Device Tree
 * compatible string involved in this tutorial.
 *
 * Load order for testing:
 *   1. insmod my_platform_device.ko
 *   2. insmod my_platform_driver.ko   -> probe() runs here
 *   3. rmmod  my_platform_driver      -> remove() runs here
 *   4. rmmod  my_platform_device
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#define DRIVER_NAME "my_platform_driver"

struct my_platform_data {
	const char *label;
	int value;
};

static int my_platform_probe(struct platform_device *pdev)
{
	struct my_platform_data *pdata;

	pr_info("%s: probe called for device \"%s\"\n", DRIVER_NAME,
		pdev->name);

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		pr_err("%s: no platform data found\n", DRIVER_NAME);
		return -EINVAL;
	}

	pr_info("%s: label = %s, value = %d\n", DRIVER_NAME,
		pdata->label, pdata->value);

	return 0;
}

static void my_platform_remove(struct platform_device *pdev)
{
	pr_info("%s: remove called for device \"%s\"\n", DRIVER_NAME,
		pdev->name);
}

/* Legacy id table - lets one driver bind to devices with a few
 * different names, and lets you pass per-variant driver_data.
 * Not required for a single-device driver like this one, but it is
 * worth knowing this exists since you will see it in older drivers.
 */
static const struct platform_device_id my_platform_id_table[] = {
	{ "my_platform_dev", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, my_platform_id_table);

static struct platform_driver my_platform_driver = {
	.probe  = my_platform_probe,
	.remove = my_platform_remove,
	.id_table = my_platform_id_table,
	.driver = {
		.name  = "my_platform_dev",  /* must match platform_device.name */
		.owner = THIS_MODULE,
	},
};

module_platform_driver(my_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Platform driver matched by name (no DT)");