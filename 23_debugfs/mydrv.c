#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kstrtox.h>

#define DRIVER_NAME  "mydrv"
#define CLASS_NAME   "mydrv"
#define BUF_SIZE     256

/* ============================================================
 * Private driver data
 * ============================================================ */
struct mydrv_dev {
    /* internal state */
    char    name[32];
    int     value;
    char    kbuf[BUF_SIZE];     /* kernel buffer backing the char device */

    /* live counters - exposed via debugfs */
    u32     open_count;
    u32     read_count;
    u32     write_count;
    u32     error_count;
    bool    debug_verbose;
    u32     reg_status;         /* fake hardware register */

    /* char device */
    dev_t           dev_nr;
    struct cdev     c_dev;

    /* device model */
    struct class           *cls;
    struct device          *class_dev;
    struct platform_device *pdev;

    /* debugfs */
    struct dentry   *dbg_dir;

    struct mutex     lock;
};

/* ============================================================
 * 1. Character device file operations
 * ============================================================ */
static int mydrv_open(struct inode *inode, struct file *file)
{
    struct mydrv_dev *dev = container_of(inode->i_cdev,
                                          struct mydrv_dev, c_dev);
    file->private_data = dev;

    mutex_lock(&dev->lock);
    dev->open_count++;
    if (dev->debug_verbose)
        dev_info(&dev->pdev->dev, "open() called - open_count=%u\n",
                 dev->open_count);
    mutex_unlock(&dev->lock);

    return 0;
}

static int mydrv_release(struct inode *inode, struct file *file)
{
    struct mydrv_dev *dev = file->private_data;

    if (dev->debug_verbose)
        dev_info(&dev->pdev->dev, "release() called\n");

    return 0;
}

static ssize_t mydrv_read(struct file *file, char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct mydrv_dev *dev = file->private_data;
    ssize_t ret;
    size_t  len;

    mutex_lock(&dev->lock);

    len = strlen(dev->kbuf);
    if (*ppos >= len) {
        ret = 0;    /* EOF */
        goto out;
    }

    count = min(count, len - (size_t)*ppos);
    if (copy_to_user(buf, dev->kbuf + *ppos, count)) {
        dev->error_count++;
        ret = -EFAULT;
        goto out;
    }

    *ppos += count;
    dev->read_count++;
    ret = count;

    if (dev->debug_verbose)
        dev_info(&dev->pdev->dev,
                 "read() %zu bytes - read_count=%u\n",
                 count, dev->read_count);
out:
    mutex_unlock(&dev->lock);
    return ret;
}

static ssize_t mydrv_write(struct file *file, const char __user *buf,
                            size_t count, loff_t *ppos)
{
    struct mydrv_dev *dev = file->private_data;
    ssize_t ret;

    if (count > BUF_SIZE - 1) {
        mutex_lock(&dev->lock);
        dev->error_count++;
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    mutex_lock(&dev->lock);

    if (copy_from_user(dev->kbuf, buf, count)) {
        dev->error_count++;
        ret = -EFAULT;
        goto out;
    }

    dev->kbuf[count] = '\0';
    dev->write_count++;

    /* update fake hardware register to reflect last write size */
    dev->reg_status = (u32)count;

    if (dev->debug_verbose)
        dev_info(&dev->pdev->dev,
                 "write() %zu bytes - write_count=%u - kbuf=%s \n",
                 count, dev->write_count, dev->kbuf);

    ret = count;
out:
    mutex_unlock(&dev->lock);
    return ret;
}

static loff_t mydrv_llseek(struct file *file, loff_t offset, int whence)
{
    struct mydrv_dev *dev = file->private_data;
    loff_t new_pos;

    mutex_lock(&dev->lock);
    switch (whence) {
    case SEEK_SET: new_pos = offset; break;
    case SEEK_CUR: new_pos = file->f_pos + offset; break;
    case SEEK_END: new_pos = strlen(dev->kbuf) + offset; break;
    default:
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    if (new_pos < 0 || new_pos > BUF_SIZE) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    file->f_pos = new_pos;
    mutex_unlock(&dev->lock);
    return new_pos;
}

static const struct file_operations mydrv_fops = {
    .owner   = THIS_MODULE,
    .open    = mydrv_open,
    .release = mydrv_release,
    .read    = mydrv_read,
    .write   = mydrv_write,
    .llseek  = mydrv_llseek,
};

/* ============================================================
 * 2. Sysfs attributes  (/sys/class/mydrv/mydrv0/)
 * ============================================================ */

/* --- name (read-only) --- */
static ssize_t name_show(struct device *d,
                          struct device_attribute *attr, char *buf)
{
    struct mydrv_dev *dev = dev_get_drvdata(d);
    return snprintf(buf, PAGE_SIZE, "%s\n", dev->name);
}
static DEVICE_ATTR_RO(name);

/* --- value (read-write) --- */
static ssize_t value_show(struct device *d,
                           struct device_attribute *attr, char *buf)
{
    struct mydrv_dev *dev = dev_get_drvdata(d);
    int val;

    mutex_lock(&dev->lock);
    val = dev->value;
    mutex_unlock(&dev->lock);

    return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t value_store(struct device *d,
                            struct device_attribute *attr,
                            const char *buf, size_t count)
{
    struct mydrv_dev *dev = dev_get_drvdata(d);
    int val, ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret < 0)
        return ret;

    mutex_lock(&dev->lock);
    dev->value = val;
    mutex_unlock(&dev->lock);

    return count;
}
static DEVICE_ATTR_RW(value);

static struct attribute *mydrv_sysfs_attrs[] = {
    &dev_attr_name.attr,
    &dev_attr_value.attr,
    NULL,
};

static struct attribute_group mydrv_sysfs_group = {
    .attrs = mydrv_sysfs_attrs,
};

/* ============================================================
 * 3. Debugfs - custom stats file using seq_file
 * ============================================================ */
static int mydrv_stats_show(struct seq_file *s, void *unused)
{
    struct mydrv_dev *dev = s->private;

    mutex_lock(&dev->lock);

    seq_printf(s, "name:          %s\n",  dev->name);
    seq_printf(s, "value:         %d\n",  dev->value);
    seq_printf(s, "kbuf:          \"%s\"\n", dev->kbuf);
    seq_printf(s, "open_count:    %u\n",  dev->open_count);
    seq_printf(s, "read_count:    %u\n",  dev->read_count);
    seq_printf(s, "write_count:   %u\n",  dev->write_count);
    seq_printf(s, "error_count:   %u\n",  dev->error_count);
    seq_printf(s, "debug_verbose: %s\n",  dev->debug_verbose ? "Y" : "N");
    seq_printf(s, "reg_status:    0x%08X\n", dev->reg_status);

    mutex_unlock(&dev->lock);
    return 0;
}

static int mydrv_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, mydrv_stats_show, inode->i_private);
}

static const struct file_operations stats_fops = {
    .owner   = THIS_MODULE,
    .open    = mydrv_stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* --- reset counters (write-only) --- */
static ssize_t mydrv_reset_write(struct file *file,
                                  const char __user *buf,
                                  size_t count, loff_t *ppos)
{
    struct mydrv_dev *dev = file->private_data;

    mutex_lock(&dev->lock);
    dev->open_count  = 0;
    dev->read_count  = 0;
    dev->write_count = 0;
    dev->error_count = 0;
    dev->reg_status  = 0;
    mutex_unlock(&dev->lock);

    dev_info(&dev->pdev->dev, "debugfs: all counters reset\n");
    return count;
}

static const struct file_operations reset_fops = {
    .owner   = THIS_MODULE,
    .open    = simple_open,
    .write   = mydrv_reset_write,
    .llseek  = noop_llseek,
};

/* ============================================================
 * 4. Platform driver probe / remove
 * ============================================================ */
static int mydrv_probe(struct platform_device *pdev)
{
    struct mydrv_dev *dev;
    int ret;

    dev_info(&pdev->dev, "probe() called\n");

    /* --- allocate private data --- */
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    platform_set_drvdata(pdev, dev);
    mutex_init(&dev->lock);

    /* --- initialise state --- */
    strscpy(dev->name, "mydrv0", sizeof(dev->name));
    dev->value      = 0;
    dev->reg_status = 0xCAFEBABE;
    strscpy(dev->kbuf, "hello", sizeof(dev->kbuf));

    /* --- char device region --- */
    ret = alloc_chrdev_region(&dev->dev_nr, 0, 1, DRIVER_NAME);
    if (ret) {
        dev_err(&pdev->dev, "alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    /* --- cdev --- */
    cdev_init(&dev->c_dev, &mydrv_fops);
    dev->c_dev.owner = THIS_MODULE;

    ret = cdev_add(&dev->c_dev, dev->dev_nr, 1);
    if (ret) {
        dev_err(&pdev->dev, "cdev_add failed: %d\n", ret);
        goto err_cdev;
    }

    /* --- class --- */
    dev->cls = class_create(CLASS_NAME);
    if (IS_ERR(dev->cls)) {
        ret = PTR_ERR(dev->cls);
        goto err_cdev_add;
    }

    /* --- device_create → /dev/mydrv0 + /sys/class/mydrv/mydrv0/ --- */
    dev->class_dev = device_create(dev->cls, &pdev->dev,
                                   dev->dev_nr, dev, "mydrv0");
    if (IS_ERR(dev->class_dev)) {
        ret = PTR_ERR(dev->class_dev);
        goto err_class;
    }

    /* --- sysfs attribute group --- */
    ret = sysfs_create_group(&dev->class_dev->kobj, &mydrv_sysfs_group);
    if (ret) {
        dev_err(&pdev->dev, "sysfs_create_group failed: %d\n", ret);
        goto err_device;
    }

    /* --- debugfs directory: /sys/kernel/debug/mydrv/ --- */
    dev->dbg_dir = debugfs_create_dir(DRIVER_NAME, NULL);

    /* variable helpers - direct pointers into private data */
    debugfs_create_u32 ("open_count",    0444, dev->dbg_dir, &dev->open_count);
    debugfs_create_u32 ("read_count",    0444, dev->dbg_dir, &dev->read_count);
    debugfs_create_u32 ("write_count",   0444, dev->dbg_dir, &dev->write_count);
    debugfs_create_u32 ("error_count",   0444, dev->dbg_dir, &dev->error_count);
    debugfs_create_bool("debug_verbose", 0644, dev->dbg_dir, &dev->debug_verbose);
    debugfs_create_x32 ("reg_status",    0444, dev->dbg_dir, &dev->reg_status);

    /* custom files */
    debugfs_create_file("stats", 0444, dev->dbg_dir, dev, &stats_fops);
    debugfs_create_file("reset", 0200, dev->dbg_dir, dev, &reset_fops);

    dev_info(&pdev->dev,
             "ready - /dev/mydrv0 · /sys/class/mydrv/ · /sys/kernel/debug/mydrv/\n");
    return 0;

    /* error path - undo in reverse order */
err_device:
    device_destroy(dev->cls, dev->dev_nr);
err_class:
    class_destroy(dev->cls);
err_cdev_add:
    cdev_del(&dev->c_dev);
err_cdev:
    unregister_chrdev_region(dev->dev_nr, 1);
    return ret;
}

static void mydrv_remove(struct platform_device *pdev)
{
    struct mydrv_dev *dev = platform_get_drvdata(pdev);

    /* debugfs first - stops any in-flight reads of our live pointers */
    debugfs_remove(dev->dbg_dir);

    /* sysfs, device, class, cdev - reverse of probe order */
    sysfs_remove_group(&dev->class_dev->kobj, &mydrv_sysfs_group);
    device_destroy(dev->cls, dev->dev_nr);
    class_destroy(dev->cls);
    cdev_del(&dev->c_dev);
    unregister_chrdev_region(dev->dev_nr, 1);

    dev_info(&pdev->dev, "remove() called - all entries removed\n");
}

/* ============================================================
 * 5. Platform device + driver registration
 * ============================================================ */
static struct platform_driver mydrv_driver = {
    .probe  = mydrv_probe,
    .remove = mydrv_remove,
    .driver = {
        .name  = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
};

static struct platform_device *mydrv_pdev;

static int __init mydrv_init(void)
{
    int ret;

    mydrv_pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
    if (IS_ERR(mydrv_pdev))
        return PTR_ERR(mydrv_pdev);

    ret = platform_driver_register(&mydrv_driver);
    if (ret) {
        platform_device_unregister(mydrv_pdev);
        return ret;
    }

    pr_info("mydrv: module loaded\n");
    return 0;
}

static void __exit mydrv_exit(void)
{
    platform_driver_unregister(&mydrv_driver);
    platform_device_unregister(mydrv_pdev);
    pr_info("mydrv: module unloaded\n");
}

module_init(mydrv_init);
module_exit(mydrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("cdev + sysfs + debugfs combined example");