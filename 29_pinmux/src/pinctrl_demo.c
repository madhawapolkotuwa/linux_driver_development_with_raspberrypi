
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/device.h>

struct pinctrl_demo_data {
	struct device		*dev;
	struct pinctrl		*pinctrl;
	struct pinctrl_state *state_default;
	struct pinctrl_state *state_alt0;
	bool is_alt0;
};

static ssize_t pin_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pinctrl_demo_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", data->is_alt0 ? "alt0" : "default");
}

static ssize_t pin_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pinctrl_demo_data *data = dev_get_drvdata(dev);
	int ret;

	if (sysfs_streq(buf, "alt0")) {
		ret = pinctrl_select_state(data->pinctrl, data->state_alt0); 
		if (ret)
			return ret;
		data->is_alt0 = true;
	} else if (sysfs_streq(buf, "default")) {
		ret = pinctrl_select_state(data->pinctrl, data->state_default);
		if (ret)
			return ret;
		data->is_alt0 = false;
	} else {
		dev_err(dev, "unknown state, use \"default\" or \"alt0\"\n");
		return -EINVAL;
	}

	dev_info(dev, "pin state switched to \"%s\" \n", buf);
	return count;
}

static DEVICE_ATTR_RW(pin_state);



static int pinctrl_demo_probe(struct platform_device *pdev)
{
	struct pinctrl_demo_data *data;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if(!data)
		return -ENOMEM;
	data->dev = &pdev->dev;

	/*
	 * devm_pinctrl_get() only fetches the handle. The "default"
	 * state is applied automatically by the driver core before
	 * probe() runs, so this line is really about getting a handle
	 * we can reuse later to switch states manually.
	 */
	data->pinctrl = devm_pinctrl_get(&pdev->dev);
	if(IS_ERR(data->pinctrl)){
		dev_err(&pdev->dev, "failed to get pinctrl handle\n");
		return PTR_ERR(data->pinctrl);
	}

	data->state_default = pinctrl_lookup_state(data->pinctrl, "default");
	if(IS_ERR(data->state_default)){
		dev_err(&pdev->dev, "failed to look up \"default\" state \n");
		return PTR_ERR(data->state_default);
	}

	data->state_alt0 = pinctrl_lookup_state(data->pinctrl, "alt0");
	if(IS_ERR(data->state_alt0)){
		dev_err(&pdev->dev, "failed to look up \"alt0\" state \n");
		return PTR_ERR(data->state_alt0);
	}

	/* Make sure we start from a known state */
	int ret = pinctrl_select_state(data->pinctrl, data->state_default);
	if(ret){
		dev_err(&pdev->dev, "failed to selct default state\n");
		return ret;
	}

	data->is_alt0 = false;

	platform_set_drvdata(pdev, data);

	ret = device_create_file(&pdev->dev, &dev_attr_pin_state);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs entry\n");
		return ret;
	}

	dev_info(&pdev->dev, "pinctrl demo probed, GPIO17 is now an output\n");
	
	return 0;
}

static void pinctrl_demo_remove(struct platform_device *pdev)
{
	struct pinctrl_demo_data *data = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_pin_state);

	/* Leave the pins back in the default (plain GPIO) state */
	pinctrl_select_state(data->pinctrl, data->state_default);

	dev_info(&pdev->dev, "pinctrl demo removed\n");
}

static const struct of_device_id pinctrl_demo_of_match[] = {
	{ .compatible = "mpcoding,pinctrl-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, pinctrl_demo_of_match);

static struct platform_driver pinctrl_demo_driver = {
	.probe  = pinctrl_demo_probe,
	.remove = pinctrl_demo_remove,
	.driver = {
		.name           = "pinctrl_demo",
		.of_match_table = pinctrl_demo_of_match,
	},
};

module_platform_driver(pinctrl_demo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Pin Control & Pinmux demo driver");