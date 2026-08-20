#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/err.h>

#define MAX_THRESHOLDS 8

struct of_parse_demo_priv {
	const char *device_name;
	u32 sample_rate_hz;
	u32 thresholds[MAX_THRESHOLDS];
	int num_thresholds;
	bool feature_enabled;
	int reset_gpio;
};

static int of_parse_demo_parse_dt(struct platform_device *pdev,
				   struct of_parse_demo_priv *priv)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;
	int count;

	if (!np) {
		dev_err(&pdev->dev, "no device tree node found\n");
		return -ENODEV;
	}

	/* ---- String property ---- */
	ret = of_property_read_string(np, "mpcoding,device-name",
				       &priv->device_name);
	if (ret) {
		dev_err(&pdev->dev, "missing mpcoding,device-name (%d)\n", ret);
		return ret;
	}

	/* ---- Single u32 property ---- */
	ret = of_property_read_u32(np, "mpcoding,sample-rate-hz",
				    &priv->sample_rate_hz);
	if (ret) {
		dev_err(&pdev->dev, "missing mpcoding,sample-rate-hz (%d)\n", ret);
		return ret;
	}

	/* ---- u32 array property ---- */
	count = of_property_count_u32_elems(np, "mpcoding,thresholds");
	if (count < 0) {
		dev_err(&pdev->dev, "missing mpcoding,thresholds (%d)\n", count);
		return count;
	}
	if (count > MAX_THRESHOLDS)
		count = MAX_THRESHOLDS;

	ret = of_property_read_u32_array(np, "mpcoding,thresholds",
					  priv->thresholds, count);
	if (ret) {
		dev_err(&pdev->dev, "failed to read mpcoding,thresholds (%d)\n", ret);
		return ret;
	}
	priv->num_thresholds = count;

	/* ---- Boolean (presence-only) property ---- */
	priv->feature_enabled = of_property_read_bool(np, "mpcoding,enable-feature");

	/* ---- GPIO property ---- */
	priv->reset_gpio = of_get_named_gpio(np, "reset-gpios", 0);
	if (priv->reset_gpio < 0) {
		dev_err(&pdev->dev, "missing reset-gpios (%d)\n", priv->reset_gpio);
		return priv->reset_gpio;
	}

	return 0;
}

static int of_parse_demo_probe(struct platform_device *pdev)
{
	struct of_parse_demo_priv *priv;
	int ret;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_parse_demo_parse_dt(pdev, priv);
	if (ret)
		return ret;

	ret = devm_gpio_request_one(&pdev->dev, priv->reset_gpio,
				     GPIOF_OUT_INIT_LOW, "of-parse-demo-reset");
	if (ret) {
		dev_err(&pdev->dev, "failed to request reset gpio %d (%d)\n",
			priv->reset_gpio, ret);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "device-name  = %s\n", priv->device_name);
	dev_info(&pdev->dev, "sample-rate  = %u Hz\n", priv->sample_rate_hz);

	for (i = 0; i < priv->num_thresholds; i++)
		dev_info(&pdev->dev, "threshold[%d] = %u\n", i, priv->thresholds[i]);

	dev_info(&pdev->dev, "feature      = %s\n",
		 priv->feature_enabled ? "enabled" : "disabled");
	dev_info(&pdev->dev, "reset-gpio   = %d\n", priv->reset_gpio);

	/* Pulse the reset line, just to prove the parsed GPIO is real and usable */
	gpio_set_value(priv->reset_gpio, 1);
	msleep(10);
	gpio_set_value(priv->reset_gpio, 0);

	dev_info(&pdev->dev, "probe complete\n");

	return 0;
}

/*
 * NOTE: as of kernel 6.11, platform_driver::remove returns void instead
 * of int. If you're building against an older kernel (pre-6.11), change
 * the return type back to int and `return 0;` at the end.
 */
static void of_parse_demo_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "remove\n");
}

static const struct of_device_id of_parse_demo_of_match[] = {
	{ .compatible = "mpcoding,of-parse-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_parse_demo_of_match);

static struct platform_driver of_parse_demo_driver = {
	.probe  = of_parse_demo_probe,
	.remove = of_parse_demo_remove,
	.driver = {
		.name           = "of_parse_demo",
		.of_match_table = of_parse_demo_of_match,
	},
};

module_platform_driver(of_parse_demo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Tutorial 30: OF (Open Firmware) Parsing - DT helper APIs demo");