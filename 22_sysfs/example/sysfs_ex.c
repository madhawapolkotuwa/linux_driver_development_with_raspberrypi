#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/kstrtox.h>

#define DRIVER_NAME   "sysfs_ex"
#define CLASS_NAME    "mysensor"
#define BIN_BLOB_SIZE 16

/* ============================================================
 * Private driver data
 * ============================================================ */
struct sensor_dev {
    /* hardware state (simulated) */
    char    name[32];
    int     value;
    u8      blob[BIN_BLOB_SIZE];   /* fake calibration data */

    /* cdev */
    dev_t                  dev_nr;
    struct cdev            c_dev;
    
    /* kernel objects */
    struct class           *cls;
    struct device          *class_dev;
    struct platform_device *pdev;

    /* workqueue - periodically updates value, calls sysfs_notify */
    struct delayed_work     dwork;

    struct mutex            lock;
};

/* ============================================================
 * 1. Standard device attributes (DEVICE_ATTR)
 * ============================================================ */

/* --- name attribute (read-only) --- */
static ssize_t name_show(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
    struct sensor_dev *sdev = dev_get_drvdata(dev);
    return snprintf(buf, PAGE_SIZE, "%s\n", sdev->name);
}
static DEVICE_ATTR_RO(name);
/* identical to DEVICE_ATTR(name, 0444, show_name, NULL); */

/* --- value attribute (read-write, notifies on write) --- */
static ssize_t value_show(struct device *dev,
                           struct device_attribute *attr, char *buf)
{
    struct sensor_dev *sdev = dev_get_drvdata(dev);
    int val;

    mutex_lock(&sdev->lock);
    val = sdev->value;
    mutex_unlock(&sdev->lock);

    return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t value_store(struct device *dev,
                            struct device_attribute *attr,
                            const char *buf, size_t count)
{
    struct sensor_dev *sdev = dev_get_drvdata(dev);
    int val;
    int ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret < 0)
        return ret;

    mutex_lock(&sdev->lock);
    sdev->value = val;
    mutex_unlock(&sdev->lock);

    /* notify any user-space poll()/select() waiting on this file */
    sysfs_notify(&dev->kobj, NULL, "value");

    dev_info(dev, "value updated to %d - sysfs_notify sent\n", val);
    return count;
}
static DEVICE_ATTR_RW(value);


/* ============================================================
 * 2. Attribute group - appears as /sys/.../config/ subdirectory
 * ============================================================ */

/* --- mode attribute (read-write, inside config/ group) --- */
static ssize_t mode_show(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "auto\n");
}

static ssize_t mode_store(struct device *dev,
                           struct device_attribute *attr,
                           const char *buf, size_t count)
{
    dev_info(dev, "mode set to: %s\n", buf);
    return count;
}
static DEVICE_ATTR_RW(mode);


/* --- threshold attribute (read-write, inside config/ group) --- */
static ssize_t threshold_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "100\n");
}

static ssize_t threshold_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    int val;
    if (kstrtoint(buf, 10, &val) < 0)
        return -EINVAL;
    dev_info(dev, "threshold set to %d\n", val);
    return count;
}
static DEVICE_ATTR_RW(threshold);


/* group definition - .name creates the config/ subdirectory */
static struct attribute *config_attrs[] = {
    &dev_attr_mode.attr,
    &dev_attr_threshold.attr,
    NULL,
};

static struct attribute_group config_group = {
    .name  = "config",      /* → /sys/class/mysensor/mysensor0/config/ */
    .attrs = config_attrs,
};


/* ============================================================
 * 3. Binary attribute - raw calibration blob
 * ============================================================ */
static ssize_t calib_read(struct file *filp, struct kobject *kobj,
                           struct bin_attribute *attr,
                           char *buf, loff_t off, size_t count)
{
    struct device    *dev  = kobj_to_dev(kobj);
    struct sensor_dev *sdev = dev_get_drvdata(dev);

    if (off >= BIN_BLOB_SIZE)
        return 0;

    count = min(count, (size_t)(BIN_BLOB_SIZE - off));
    memcpy(buf, sdev->blob + off, count);
    return count;
}

static ssize_t calib_write(struct file *filp, struct kobject *kobj,
                            struct bin_attribute *attr,
                            char *buf, loff_t off, size_t count)
{
    struct device    *dev  = kobj_to_dev(kobj);
    struct sensor_dev *sdev = dev_get_drvdata(dev);

    if (off >= BIN_BLOB_SIZE)
        return -ENOSPC;

    count = min(count, (size_t)(BIN_BLOB_SIZE - off));
    memcpy(sdev->blob + off, buf, count);
    dev_info(dev, "calibration blob updated (%zu bytes at offset %lld)\n",
             count, off);
    return count;
}

static BIN_ATTR(calibration, 0644, calib_read, calib_write, BIN_BLOB_SIZE);


/* ============================================================
 * 4. Workqueue - simulates hardware updates + sysfs_notify
 * ============================================================ */
static void sensor_work_fn(struct work_struct *work)
{
    struct sensor_dev *sdev =
        container_of(work, struct sensor_dev, dwork.work);

    mutex_lock(&sdev->lock);
    sdev->value++;           /* simulate a sensor reading changing */
    mutex_unlock(&sdev->lock);

    dev_info(&sdev->pdev->dev,
             "sensor tick - value=%d, notifying user space\n", sdev->value);

    /* wake up any poll()/select() waiting on the 'value' attribute */
    sysfs_notify(&sdev->class_dev->kobj, NULL, "value");

    /* reschedule - fires every 5 seconds */
    schedule_delayed_work(&sdev->dwork, msecs_to_jiffies(5000));
}


static struct file_operations fops = {
    /* Just Empty File operations */
};

/* ============================================================
 * 5. Platform driver probe / remove
 * ============================================================ */
static int sensor_probe(struct platform_device *pdev)
{
    struct sensor_dev *sdev;
    int ret;

    dev_info(&pdev->dev, "probe called\n");

    /* allocate private data - device-managed, auto-freed on remove */
    sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
    if (!sdev)
        return -ENOMEM;

    sdev->pdev = pdev;
    platform_set_drvdata(pdev, sdev);
    mutex_init(&sdev->lock);

    /* initialise simulated hardware state */
    strscpy(sdev->name, "mysensor0", sizeof(sdev->name));
    sdev->value = 0;
    memset(sdev->blob, 0xAB, BIN_BLOB_SIZE);   /* fake calibration data */

    alloc_chrdev_region(&sdev->dev_nr, 0, 1, DRIVER_NAME);

    cdev_init(&sdev->c_dev, &fops);
    sdev->c_dev.owner = THIS_MODULE;

    ret = cdev_add(&sdev->c_dev, sdev->dev_nr, 1);
    if(ret) goto err_cdev_add;

    /* --- class_create --- */
    sdev->cls = class_create(CLASS_NAME);
    if (IS_ERR(sdev->cls))
        return PTR_ERR(sdev->cls);

    /* --- device_create: /sys/class/mysensor/mysensor0/ + /dev/mysensor0 --- */
    sdev->class_dev = device_create(sdev->cls, &pdev->dev,
                                    sdev->dev_nr, sdev, "mysensor0");
    if (IS_ERR(sdev->class_dev)) {
        ret = PTR_ERR(sdev->class_dev);
        goto err_class;
    }

    /* --- standard attributes: name, value --- */
    ret = device_create_file(sdev->class_dev, &dev_attr_name);
    if (ret) goto err_device;

    ret = device_create_file(sdev->class_dev, &dev_attr_value);
    if (ret) goto err_name;

    /* --- attribute group: config/ subdirectory --- */
    ret = sysfs_create_group(&sdev->class_dev->kobj, &config_group);
    if (ret) goto err_value;

    /* --- binary attribute: calibration blob --- */
    ret = device_create_bin_file(sdev->class_dev, &bin_attr_calibration);
    if (ret) goto err_group;

    /* --- workqueue: simulate sensor ticks --- */
    INIT_DELAYED_WORK(&sdev->dwork, sensor_work_fn);
    schedule_delayed_work(&sdev->dwork, msecs_to_jiffies(5000));

    dev_info(&pdev->dev, "ready - sysfs entries created\n");
    return 0;

    /* error path - undo in reverse order */
err_group:
    sysfs_remove_group(&sdev->class_dev->kobj, &config_group);
err_value:
    device_remove_file(sdev->class_dev, &dev_attr_value);
err_name:
    device_remove_file(sdev->class_dev, &dev_attr_name);
err_device:
    device_destroy(sdev->cls, sdev->dev_nr);
err_class:
    class_destroy(sdev->cls);
err_cdev_add:
    unregister_chrdev_region(sdev->dev_nr, 1); 

    return ret;
}

static void sensor_remove(struct platform_device *pdev)
{
    struct sensor_dev *sdev = platform_get_drvdata(pdev);

    /* stop workqueue first - no more sysfs_notify after this */
    cancel_delayed_work_sync(&sdev->dwork);

    /* remove sysfs entries in reverse order */
    device_remove_bin_file(sdev->class_dev, &bin_attr_calibration);
    sysfs_remove_group(&sdev->class_dev->kobj, &config_group);
    device_remove_file(sdev->class_dev, &dev_attr_value);
    device_remove_file(sdev->class_dev, &dev_attr_name);
    device_destroy(sdev->cls, sdev->dev_nr);
    class_destroy(sdev->cls);

    unregister_chrdev_region(sdev->dev_nr, 1);

    dev_info(&pdev->dev, "remove called - all sysfs entries removed\n");
}


/* ============================================================
 * 6. Platform device + driver registration
 * ============================================================ */
static struct platform_driver sensor_driver = {
    .probe  = sensor_probe,
    .remove = sensor_remove,
    .driver = {
        .name  = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
};

static struct platform_device *sensor_pdev;

static int __init sysfs_ex_init(void)
{
    int ret;

    sensor_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
    if (IS_ERR(sensor_pdev))
        return PTR_ERR(sensor_pdev);

    ret = platform_driver_register(&sensor_driver);
    if (ret) {
        platform_device_unregister(sensor_pdev);
        return ret;
    }

    pr_info("sysfs_ex: module loaded\n");
    return 0;
}

static void __exit sysfs_ex_exit(void)
{
    platform_driver_unregister(&sensor_driver);
    platform_device_unregister(sensor_pdev);
    pr_info("sysfs_ex: module unloaded\n");
}

module_init(sysfs_ex_init);
module_exit(sysfs_ex_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Sysfs Attributes - binary attrs, groups, sysfs_notify");