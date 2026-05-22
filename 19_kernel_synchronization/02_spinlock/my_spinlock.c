#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

#include <linux/spinlock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Kernel Synchronization: Spinlock");

static const char *my_device = "my_spinlock";
static dev_t dev_nr;
static struct cdev my_cdev;
static struct class *my_class;

static DEFINE_SPINLOCK(my_spinlock);

/* GPIO Button */
#define BUTTON_GPIO 20
#define IO_OFFSET 512
static int button_gpio = (BUTTON_GPIO + IO_OFFSET);

static struct gpio_desc *button; /* GPIO descriptor */
/* Variable contains irq number */
unsigned int irq_number;

/* Size of the device's internal buffer */
#define SHARED_BUFFER_SIZE 64

/* Internal device buffer (kernel memory) and it's mutex */
static char *shared_buffer;
static int shared_len;

static int my_open(struct inode *pInode, struct file *pFile){
    pr_info("%s: my_open called, Major: %d Minor: %d\n", my_device, imajor(pInode), iminor(pInode));
    return 0;
}
static int my_release(struct inode *pInode, struct file *pFile){
    pr_info("%s: my_release called, file is closed\n", my_device);
    return 0;
}

/* read: copy data from kernel buffer -> user-space buffer*/
static	ssize_t my_read(struct file *pFile, char __user *pUser_buff, size_t count, loff_t *pOffset){

    char local_buffer[SHARED_BUFFER_SIZE];
    unsigned long flags;

    spin_lock_irqsave(&my_spinlock, flags);

    /* Critical section */
    memcpy(local_buffer, shared_buffer, shared_len);
    /* End of critical section */

    spin_unlock_irqrestore(&my_spinlock, flags);

    /* copy_to_user OUTSIDE the spinlock - it can sleep */
    return copy_to_user(pUser_buff, local_buffer, shared_len) ? -EFAULT : shared_len;
}

/* file operations structure */
static struct file_operations fops = {
    .open = my_open,
    .release = my_release,
    .read = my_read,
};

static int count = 1;
/* ------------------------------------------------------------------ */
/*  GPIO Interrupt Service Routine                                    */
/* ------------------------------------------------------------------ */
static irqreturn_t isr(int irq, void *dev_id){
    pr_info("%s: GPIO Interrupt occoured\n", my_device);

    spin_lock(&my_spinlock);
    /* --- Critical section --- */
    snprintf(shared_buffer, SHARED_BUFFER_SIZE, "Button pressed! %d\n", count++);
    shared_len = strlen(shared_buffer);
    /* --- End of critical section --- */
    spin_unlock(&my_spinlock);

    return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/*  GPIO / IRQ setup                                                  */
/* ------------------------------------------------------------------ */

static int external_gpio_irq_setup(unsigned int gpio){
    int status;

    /* get GPIO descriptor for pin <gpio> */
    button = gpio_to_desc(gpio);
    if(!button){
        pr_err("%s: unable to get GPIO descriptor for gpio: %d\n", my_device, gpio);
        return -EINVAL;
    }

    /* set gpio direction to input */
    status = gpiod_direction_input(button);
    if(status){
        pr_err("%s: Failed to set GPIO %d as input\n", my_device, gpio);
        return status;
    }

    /* Set gpi ISR */
    irq_number = gpiod_to_irq(button);
    status = request_irq(irq_number, isr, IRQF_TRIGGER_FALLING, "irq_handler", NULL);
    if(status){
        pr_info("%s: Failed to request IRQ\n", my_device);
        return status;
    }

    return 0;
} 

static int __init my_init(void) {
    int status;
    /* allocate kernel buffer */
    shared_buffer = kmalloc(SHARED_BUFFER_SIZE, GFP_KERNEL);
    if(!shared_buffer){
        pr_err("%s: Failed to allocate device buffer\n", my_device);
        return -ENOMEM;
    }

    spin_lock_init(&my_spinlock);

    /* clear shared_buffer */
    memset(shared_buffer, 0, SHARED_BUFFER_SIZE);

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

    status = external_gpio_irq_setup(button_gpio);
    if(status) { // set gpio 20 as exteranl gpio interrupt
       pr_err("%s: GPIO %d failed to set up as external interrupt\n", my_device, button_gpio);
       goto free_gpio;
    }

    pr_info("%s: Caracter device registerd, Major number: %d Minor number: %d\n",my_device, MAJOR(dev_nr), MINOR(dev_nr));
    pr_info("%s: Created device number under /sys/class/my_class\n", my_device);
    pr_info("%s: Created new device node /dev/my_cdev0\n", my_device);
    return 0;

free_gpio:
delete_class:
    class_destroy(my_class);

delete_cdev:
    cdev_del(&my_cdev);

free_device_nr:
    unregister_chrdev_region(dev_nr, MINORMASK + 1); 

    return status;

}

static void __exit my_exit(void){
    
    free_irq(irq_number, NULL);
    kfree(shared_buffer);

    device_destroy(my_class, MKDEV(MAJOR(dev_nr), 0));
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_nr, MINORMASK + 1);
    pr_info("%s: Goodbye, Kernel\n", my_device);
}

module_init(my_init);
module_exit(my_exit);