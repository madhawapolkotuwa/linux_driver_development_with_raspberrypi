#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Our first character device");

static const char *my_device = "my_cdev";

static dev_t dev_nr;

static struct cdev my_cdev;

static struct class *my_class;

/* Size of the device's internal buffer */
#define DEV_BUFFER_SIZE 64

/* Internal device buffer (kernel memory) and it's mutex */
static char *dev_buffer;
static struct mutex dev_mutex;

static int my_open(struct inode *pInode, struct file *pFile){
    pr_info("%s: my_open called, Major: %d Minor: %d\n", my_device, imajor(pInode), iminor(pInode));

    pr_info("%s: file->f_pos: %lld\n", my_device, pFile->f_pos);
    pr_info("%s: file->f_mode: 0x%x\n", my_device, pFile->f_mode);
    pr_info("%s: file->f_flags: 0x%x\n", my_device, pFile->f_flags);
    return 0;
}
static int my_release(struct inode *pInode, struct file *pFile){
    pr_info("%s: my_release called, file is closed\n", my_device);
    return 0;
}

/* read: copy data from kernel buffer -> user-space buffer*/
static	ssize_t my_read(struct file *pFile, char __user *pUser_buff, size_t count, loff_t *pOffset){
    size_t bytes_to_copy, not_copied, copied;

    if(mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;

    pr_info("%s: read called: request=%zu, offset=%lld, \n", my_device, count, *pOffset);

    bytes_to_copy = (count + *pOffset > strlen(dev_buffer)) ? (strlen(dev_buffer) - *pOffset) : count;

    pr_info("%s: Read will copy=%zu bytes\n", my_device, bytes_to_copy);

    not_copied = copy_to_user(pUser_buff, dev_buffer + *pOffset, bytes_to_copy);
    copied = bytes_to_copy - not_copied;

    *pOffset += copied;

    if(not_copied)
        pr_warn("%s: copy_to_user only copied %zu/%zu\n", my_device, copied, bytes_to_copy);

    pr_info("%s: Read done: return=%zu, new offset=%lld\n", my_device, copied, *pOffset);

    mutex_unlock(&dev_mutex);
    return (ssize_t)copied;

}

/* write: copy data from user-space buffer -> kernel buffer */
static	ssize_t my_write(struct file *pFile, const char __user *pUser_buff, size_t count, loff_t *pOffset){
    size_t bytes_to_copy, not_copied, copied;

    if(mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;
    
    if(*pOffset >= DEV_BUFFER_SIZE){
        pr_info("%s: no space left to write\n", my_device);
        mutex_unlock(&dev_mutex);
        return -ENOSPC;
    }else if(count > DEV_BUFFER_SIZE - (size_t)*pOffset){
        bytes_to_copy = DEV_BUFFER_SIZE - (size_t)*pOffset;
        pr_info("%s: only %zu bytes left to write\n", my_device, bytes_to_copy);
    }else{
        bytes_to_copy = count;
        pr_info("%s: writing %zu bytes\n", my_device, bytes_to_copy);
    }

    pr_info("%s: write request=%zu, offset=%lld, will copy=%zu\n", my_device, count, *pOffset, bytes_to_copy);

    not_copied = copy_from_user(dev_buffer + *pOffset, pUser_buff, bytes_to_copy);
    copied = bytes_to_copy - not_copied;

    if(not_copied){
        pr_warn("%s: copy_from_user: only copied %zu/%zu\n", my_device, copied, bytes_to_copy);
    }

    *pOffset += copied;

    pr_info("%s: write done: copied=%zu, new offset=%lld\n", my_device, copied, *pOffset);

    mutex_unlock(&dev_mutex);
    return (size_t)copied;
}

/* file operations structure */
static struct file_operations fops = {
    .open = my_open,
    .release = my_release,
    .read = my_read,
    .write = my_write,
};

static int __init my_init(void) {
    int status;

    /* allocate kernel buffer */
    dev_buffer = kmalloc(DEV_BUFFER_SIZE, GFP_KERNEL);
    if(!dev_buffer){
        pr_err("%s: Failed to allocate device buffer\n", my_device);
        return -ENOMEM;
    }
    /* initialize mutex */
    mutex_init(&dev_mutex);
    /* clear dev_buffer */
    memset(dev_buffer, 0, DEV_BUFFER_SIZE);

    status = alloc_chrdev_region(&dev_nr, 0, MINORMASK + 1, my_device);
    if(status){
        pr_err("%s: character device registation failed\n", my_device);
        return status;
    }

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;

    status = cdev_add(&my_cdev, dev_nr, MINORMASK + 1);
    if(status){
        pr_err("%s: error adding cdev\n", my_device);
        goto free_device_nr;
    }

    my_class = class_create("my_class");
    if(!my_class){
        pr_err("%s: Could not create class my_class\n",my_device);
        status = ENOMEM;
        goto delete_cdev;
    }

    if(!device_create(my_class, NULL, dev_nr, NULL, "my_cdev%d", 0)){
        pr_err("%s: Could not create device my_cdev0\n", my_device);
        status = ENOMEM;
        goto delete_class;
    }

    pr_info("%s: Caracter device registerd, Major number: %d Minor number: %d\n",my_device, MAJOR(dev_nr), MINOR(dev_nr));
    pr_info("%s: Created device number under /sys/class/my_class\n", my_device);
    pr_info("%s: Created new device node /dev/my_cdev\n", my_device);
    return 0;

delete_class:
    class_destroy(my_class);

delete_cdev:
    cdev_del(&my_cdev);

free_device_nr:
    unregister_chrdev_region(dev_nr, MINORMASK + 1); 

    return status;

}

static void __exit my_exit(void){
    device_destroy(my_class, dev_nr);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_nr, MINORMASK + 1);
    pr_info("%s: Goodbye, Kernel\n", my_device);
}

module_init(my_init);
module_exit(my_exit);