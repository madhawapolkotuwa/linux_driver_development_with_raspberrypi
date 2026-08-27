#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#define DEVICE_NAME "misc_demo"
#define BUFFER_SIZE 128
static char device_buffer[BUFFER_SIZE];
static size_t data_size;

/* Called when a user-space process opens /dev/misc_demo */
static int misc_demo_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened, device No: %d %d\n", DEVICE_NAME, imajor(inode), iminor(inode));

    return 0;
}

/* Called when a user-space process closes /dev/misc_demo */
static int misc_demo_release(struct inode *inode, struct file *file)
{
    pr_info("%s: device closed\n", DEVICE_NAME);
    return 0;
}

/* Called when a user-space process reads from /dev/misc_demo */
static ssize_t misc_demo_read(struct file *file, char __user *user_buf,
                               size_t len, loff_t *offset)
{
    ssize_t ret;

    if (*offset >= data_size)
        return 0; /* EOF */

    if (len > data_size - *offset)
        len = data_size - *offset;

    if (copy_to_user(user_buf, device_buffer + *offset, len)) {
        pr_err("%s: copy_to_user failed\n", DEVICE_NAME);
        return -EFAULT;
    }

    *offset += len;
    ret = len;

    pr_info("%s: read %zd bytes\n",DEVICE_NAME, ret);
    return ret;
}

/* Called when a user-space process writes to /dev/misc_demo */
static ssize_t misc_demo_write(struct file *file, const char __user *user_buf,
                                size_t len, loff_t *offset)
{
    if (len > BUFFER_SIZE)
        len = BUFFER_SIZE;

    if (copy_from_user(device_buffer, user_buf, len)) {
        pr_err("%s: copy_from_user failed\n", DEVICE_NAME);
        return -EFAULT;
    }

    data_size = len;
    *offset = 0; /* reset offset so next read starts from beginning */

    pr_info("%s: wrote %zu bytes\n",DEVICE_NAME, len);
    return len;
}

/* File operations structure */
static const struct file_operations misc_demo_fops = {
    .owner   = THIS_MODULE,
    .open    = misc_demo_open,
    .release = misc_demo_release,
    .read    = misc_demo_read,
    .write   = misc_demo_write,
};

/* Misc device structure */
static struct miscdevice misc_demo_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &misc_demo_fops,
    .mode  = 0666, /* read and write permission */
};

/* Module init: register the misc device */
static int __init misc_demo_init(void)
{
    int ret;

    ret = misc_register(&misc_demo_device);
    if (ret) {
        pr_err("%s: failed to register misc device\n", DEVICE_NAME);
        return ret;
    }

    pr_info("%s: registered as /dev/%s (minor = %d)\n",
            DEVICE_NAME,DEVICE_NAME, misc_demo_device.minor);

    return 0;
}

/* Module exit: deregister the misc device */
static void __exit misc_demo_exit(void)
{
    misc_deregister(&misc_demo_device);
    pr_info("%s: device unregistered\n",DEVICE_NAME);
}

module_init(misc_demo_init);
module_exit(misc_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Simple Misc Device Driver Example");