#include "mybus.h"

/*******************************************
 * Create one fake device
 *******************************************/
static struct my_device mydev = {
    .name = "mydev0",
};

static ssize_t my_show_type(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    struct my_device *mydev = container_of(dev, struct my_device, dev);
    return snprintf(buf, PAGE_SIZE, "type : %s\n", mydev->name);
}

/* Defines dev_attr_type - visible as /sys/devices/mybus0/mydev0/type */
DEVICE_ATTR(type, 0444, my_show_type, NULL);

static int __init mydevice_init(void)
{
    int ret;

    pr_info("mydevice: init\n");

    ret = my_register_device(&mydev);
    if (ret)
        return ret;

    ret = device_create_file(&mydev.dev, &dev_attr_type);
    if (ret)
        pr_err("Failed to device bus attribute\n");

    pr_info("mydevice: device registered\n");

    return 0;
}

static void __exit mydevice_exit(void)
{
    pr_info("mydevice: exit\n");

    device_remove_file(&mydev.dev, &dev_attr_type);

    my_unregister_device(&mydev);
}

module_init(mydevice_init);
module_exit(mydevice_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding -LDD");
MODULE_DESCRIPTION("Fake Device");