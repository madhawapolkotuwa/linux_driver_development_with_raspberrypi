#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/timer.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("fasync demo - async notification with GPIO interrupt");

#define BUTTON_GPIO 20
#define IO_OFFSET 512
static int button_gpio = (BUTTON_GPIO + IO_OFFSET);

static const char       *my_device = "my_cdev";

static dev_t            dev_nr;
static struct cdev      my_cdev;
static struct class     *my_class;

/* Variable contains irq number */
unsigned int irq_number;

static char message[] = "Hello from Kernel!\n";

/* Linked list of processes that registered for async notification on this device */
static struct fasync_struct *async_queue;

static int data_available = 0;

/* ------------------------------------------------------------------ */
/*  File operations                                                   */
/* ------------------------------------------------------------------ */

static ssize_t my_read(struct file *file,
                       char __user *buf,
                       size_t len,
                       loff_t *off)
{
    pr_info("%s: read called\n", my_device);

    if(!data_available){
        return -EAGAIN;
    }

    data_available = 0;

    if (copy_to_user(buf, message, sizeof(message)))
        return -EFAULT;

    return sizeof(message);
}

static int my_fasync(int fd, struct file *file, int on){

    pr_info("%s: fasync called\n",my_device);

    return fasync_helper(fd, file, on, &async_queue);

}


static int my_open(struct inode *inode, struct file *file)
{
    pr_info("%s: device opened\n", my_device);
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    /* Remove this fd from the async notification list */
    my_fasync(-1, file, 0);
    pr_info("%s: device closed\n", my_device);
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .read = my_read,
    .fasync = my_fasync,
    .release = my_release,
};


/* ------------------------------------------------------------------ */
/*  GPIO Interrupt Service Routine                                    */
/* ------------------------------------------------------------------ */

static irqreturn_t isr(int irq, void *dev_id){
    pr_info("%s: GPIO Interrupt occurred\n", my_device);

    data_available = 1;

    kill_fasync(&async_queue, SIGIO, POLL_IN);
    
    return IRQ_HANDLED;
}

static int external_gpio_irq_setup(unsigned int gpio){
    int status;

    /* request GPIO */
    status = gpio_request(gpio, "button_gpio");
    if(status){
        pr_err("%s: Failed to request GPIO %d\n", my_device, gpio);
        return status;
    }

    /* set gpio direction to input */
    status = gpio_direction_input(gpio);
    if(status){
        pr_err("%s: Failed to set GPIO %d as input\n", my_device, gpio);
        gpio_free(gpio);
        return status;
    }

    /* Set gpi ISR */
    irq_number = gpio_to_irq(gpio);
    if(irq_number < 0){
        pr_err("%s: Failed to get IRQ number for the button GPIO %d\n", my_device, irq_number);
        gpio_free(gpio);
        return irq_number;
    }

    status = request_irq(irq_number, isr, IRQF_TRIGGER_FALLING, "irq_handler", NULL);
    if(status){
        pr_info("%s: Failed to request IRQ\n", my_device);
        gpio_free(gpio);
        return status;
    }

    return 0;
} 

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                */
/* ------------------------------------------------------------------ */

static int __init my_init(void) {
    int status;
    status = alloc_chrdev_region(&dev_nr, 0, MINORMASK + 1, my_device);
    if(status){
        pr_err("%s: Character device registration failed\n", my_device);
        return status;
    }

    cdev_init(&my_cdev, &fops);

    status = cdev_add(&my_cdev, dev_nr, MINORMASK + 1);
    if(status){
        pr_err("%s: cdev_add failed\n", my_device);
        goto free_device_nr;
    }

    my_class = class_create("my_class");
    if(!my_class){
        pr_err("%s:  class_create failed\n",my_device);
        status = ENOMEM;
        goto delete_cdev;
    }

    if(!device_create(my_class, NULL, dev_nr, NULL, "my_cdev%d", 0)){
        pr_err("%s: device_create failed\n", my_device);
        status = ENOMEM;
        goto delete_class;
    }

    if(external_gpio_irq_setup(button_gpio)){ // set gpio 20 as exteranl gpio interrupt
       pr_err("%s: GPIO %d failed to set up as external interrupt\n", my_device, button_gpio);
       goto free_gpio;
    }

    pr_info("%s: Caracter device registerd, Major number: %d Minor number: %d\n",my_device, MAJOR(dev_nr), MINOR(dev_nr));
    pr_info("%s: Created device number under /sys/class/my_class\n", my_device);
    pr_info("%s: Created new device node /dev/my_cdev0\n", my_device);
    pr_info("%s: GPIO %d configured as falling-edge interrupt\n", my_device, BUTTON_GPIO);

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
    gpio_free(button_gpio);

    device_destroy(my_class, dev_nr);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_nr, MINORMASK + 1);
    pr_info("%s: Module unloaded\n", my_device);
}

module_init(my_init);
module_exit(my_exit);



