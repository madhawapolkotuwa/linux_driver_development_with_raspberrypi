#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#define DRIVER_NAME "dt_probe"

static struct of_device_id my_driver_ids[] = {
    { .compatible = "testdevice,mydev", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_driver_ids);

static int dt_probe(struct platform_device *pdev){
    struct device *dev = &pdev->dev;
    int ret, i;

    const char *label;
    u32 my_value;
    u8 my_small_val;
    u32 my_array[4];
    const char *my_stringlist[3];

    pr_info("%s: Entered probe function\n", DRIVER_NAME);

    /* --- String --- */
    if(!device_property_present(dev, "label")){
        dev_err(dev, "%s: Property 'label' not found \n", DRIVER_NAME);
        return -ENODEV;
    }
    ret = device_property_read_string(dev, "label", &label);
    if(ret) {
        dev_err(dev, "%s: Cluld not read 'label' \n", DRIVER_NAME);
        return ret;
    }
    pr_info("%s: label: %s\n", DRIVER_NAME, label);

    /* --- u32 --- */
    ret = device_property_read_u32(dev, "my_value", &my_value);
    if(ret) {
        dev_err(dev, "%s: Cluld not read 'my_value' \n", DRIVER_NAME);
        return ret;
    }
    pr_info("%s: my_value: %u\n", DRIVER_NAME, my_value);

    /* --- u8 --- */
    ret = device_property_read_u8(dev, "my_small_val", &my_small_val);
    if (ret) { 
        dev_err(dev, "%s: Could not read 'my_small_val'\n", DRIVER_NAME); 
        return ret; 
    }
    pr_info("%s: my_small_val: 0x%02X\n", DRIVER_NAME, my_small_val);

    /* --- Boolean (presence check) --- */
    if (device_property_present(dev, "my_flag"))
        pr_info("%s: my_flag is SET\n", DRIVER_NAME);
    else
        pr_info("%s: my_flag is NOT set\n", DRIVER_NAME);

    /* --- u32 array --- */
    ret = device_property_read_u32_array(dev, "my_array", my_array, ARRAY_SIZE(my_array));
    if (ret) { 
        dev_err(dev, "%s: Could not read 'my_array'\n", DRIVER_NAME); 
        return ret; 
    }
    for (i = 0; i < ARRAY_SIZE(my_array); i++)
        pr_info("%s: my_array[%d]: %u\n", DRIVER_NAME, i, my_array[i]);

    /* --- String list --- */
    ret = device_property_read_string_array(dev, "my_stringlist",
                                            my_stringlist, ARRAY_SIZE(my_stringlist));
    if (ret < 0) { 
        dev_err(dev, "%s: Could not read 'my_stringlist'\n", DRIVER_NAME); 
        return ret; 
    }
    for (i = 0; i < ARRAY_SIZE(my_stringlist); i++)
        pr_info("%s: my_stringlist[%d]: %s\n", DRIVER_NAME, i, my_stringlist[i]);


    return 0;
}

static void dt_remove(struct platform_device *pdev){
    pr_info("%s: Remove function called\n", DRIVER_NAME);
}

static struct platform_driver my_driver = {
    .probe = dt_probe,
    .remove = dt_remove,
    .driver = {
        .name = "my_device_driver",
        .of_match_table = my_driver_ids,
    },
};

static int __init my_init(void) {
    int ret = platform_driver_register(&my_driver);
    if(ret){
        pr_err("%s: Error registering platform_driver\n", DRIVER_NAME);
        return ret;
    }
    pr_info("%s: Loaded the platform driver\n", DRIVER_NAME);
    return 0;
}

static void __exit my_exit(void) {
    platform_driver_unregister(&my_driver);
    pr_info("%s: Unloading driver\n", DRIVER_NAME);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("A simple module to read Device Tree overlay properties");

