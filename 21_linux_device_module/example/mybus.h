#ifndef MYBUS_H
#define MYBUS_H

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/string.h>

/* ****************************
 * Device structure
 * **************************** */
struct my_device {
    char *name;
    struct device dev;
};

/* ****************************
 * Driver structure
 * **************************** */
struct my_driver {
    struct device_driver driver;
};

/* ****************************
 * Bus registration APIs
 * **************************** */
extern struct bus_type my_bus_type;

int my_register_device(struct my_device *dev);
void my_unregister_device(struct my_device *dev);

int my_register_driver(struct my_driver *drv);
void my_unregister_driver(struct my_driver *drv);
 

#endif /* MYBUS_H */
