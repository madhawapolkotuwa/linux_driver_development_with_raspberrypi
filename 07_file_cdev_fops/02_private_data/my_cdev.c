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

static struct mutex dev_mutex;

static int my_open(struct inode *pInode, struct file *pFile){
    pr_info("%s: my_open called, Major: %d Minor: %d\n", my_device, imajor(pInode), iminor(pInode));

    char *buffer = kzalloc(DEV_BUFFER_SIZE, GFP_KERNEL);
    if(!buffer)
        return -ENOMEM;

    pFile->private_data = buffer;

    pr_info("%s: Allocated %d bytes for private data\n", my_device, DEV_BUFFER_SIZE);

    return 0;
}
static int my_release(struct inode *pInode, struct file *pFile){

    char *buffer = (char *)pFile->private_data;
    if(buffer)
        kfree(buffer);

    pr_info("%s: my_release called, freed %d bytes and file is closed\n", my_device, DEV_BUFFER_SIZE);
    return 0;
}

/* read: copy data from kernel buffer -> user-space buffer*/
static	ssize_t my_read(struct file *pFile, char __user *pUser_buff, size_t count, loff_t *pOffset){
    size_t bytes_to_copy, not_copied, copied;

    char *dev_buffer = (char *)pFile->private_data;
    if(!dev_buffer)
        return -EINVAL;

    if(mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;

    pr_info("%s: read called: request=%zu, \n", my_device, count);

    bytes_to_copy = (count > strlen(dev_buffer)) ? strlen(dev_buffer) : count;

    pr_info("%s: Read will copy=%zu bytes\n", my_device, bytes_to_copy);

    not_copied = copy_to_user(pUser_buff, dev_buffer, bytes_to_copy);
    copied = bytes_to_copy - not_copied;

    

    if(not_copied)
        pr_warn("%s: copy_to_user only copied %zu/%zu\n", my_device, copied, bytes_to_copy);

    pr_info("%s: Read done: return=%zu\n", my_device, copied);

    mutex_unlock(&dev_mutex);
    return (ssize_t)copied;
}

/* write: copy data from user-space buffer -> kernel buffer */
static	ssize_t my_write(struct file *pFile, const char __user *pUser_buff, size_t count, loff_t *pOffset){
    size_t bytes_to_copy, not_copied, copied;

    char *dev_buffer = (char *)pFile->private_data;
    if(!dev_buffer)
        return -EINVAL;

    if(mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;
    
    bytes_to_copy = count > DEV_BUFFER_SIZE ? DEV_BUFFER_SIZE : count;

    pr_info("%s: write request=%zu, will copy=%zu\n", my_device, count, bytes_to_copy);

    not_copied = copy_from_user(dev_buffer, pUser_buff, bytes_to_copy);
    copied = bytes_to_copy - not_copied;

    if(not_copied){
        pr_warn("%s: copy_from_user: only copied %zu/%zu\n", my_device, copied, bytes_to_copy);
    }

    pr_info("%s: write done: copied=%zu\n", my_device, copied);

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
    
    /* initialize mutex */
    mutex_init(&dev_mutex);

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