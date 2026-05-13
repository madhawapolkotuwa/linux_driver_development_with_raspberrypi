#include "mybus.h"

/* ******************************************
 * Match function
 * device name == driver name
 ********************************************/
static int my_match(struct device *dev,
                    const struct device_driver *drv)
{
    pr_info("mybus: match called\n");
    return !strcmp(dev_name(dev), drv->name);
}

/* uevent: add a custom environment variable for udev */
static int my_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEV_NAME=%s", dev_name(dev));
    return 0;
}

/*******************************************
 * Custom bus definition
 *******************************************/
struct bus_type my_bus_type = {
    .name  = "mybus",
    .match = my_match,
    .uevent = my_uevent,
};
EXPORT_SYMBOL(my_bus_type);

/* bus attributes */
//  Show function
static ssize_t version_show(const struct bus_type *bus, char *buf){
    return sprintf(buf, "1.0\n");
}

//  Define attribute (bus_attr_version)
static BUS_ATTR_RO(version);


/*******************************************
 * Parent bus device
 * appears in /sys/devices/mybus0
 *******************************************/
static void my_bus_release(struct device *dev)
{
    pr_info("mybus: release called\n");
}

static struct device my_bus_device = {
    .init_name = "mybus0",
    .release   = my_bus_release,
};

/********************************************
 * Device registration
 ********************************************/
static void my_dev_release(struct device *dev)
{
    pr_info("mybus: device release\n");
}

int my_register_device(struct my_device *mydev)
{
    mydev->dev.bus = &my_bus_type;
    mydev->dev.parent = &my_bus_device;
    mydev->dev.release = my_dev_release;

    dev_set_name(&mydev->dev, mydev->name);

    return device_register(&mydev->dev);
}
EXPORT_SYMBOL(my_register_device);

void my_unregister_device(struct my_device *mydev)
{
    device_unregister(&mydev->dev);
}
EXPORT_SYMBOL(my_unregister_device);

/********************************************
 * Driver registration
 ********************************************/
int my_register_driver(struct my_driver *drv)
{
    drv->driver.bus = &my_bus_type;
    return driver_register(&drv->driver);
}
EXPORT_SYMBOL(my_register_driver);

void my_unregister_driver(struct my_driver *drv)
{
    driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(my_unregister_driver);

/********************************************
 * Module init / exit
 ********************************************/
static int __init mybus_init(void)
{
    int ret;

    pr_info("mybus: init\n");

    ret = bus_register(&my_bus_type);
    if (ret)
        return ret;

    ret = bus_create_file(&my_bus_type, &bus_attr_version);
    if (ret)
        pr_err("Failed to create bus attribute\n");

    ret = device_register(&my_bus_device);
    if (ret) {
        bus_unregister(&my_bus_type);
        return ret;
    }

    pr_info("mybus: registered\n");
    return 0;
}

static void __exit mybus_exit(void)
{
    pr_info("mybus: exit\n");
    bus_remove_file(&my_bus_type, &bus_attr_version);
    
    device_unregister(&my_bus_device);
    bus_unregister(&my_bus_type);
}

module_init(mybus_init);
module_exit(mybus_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Custom Linux Device Model Bus");

