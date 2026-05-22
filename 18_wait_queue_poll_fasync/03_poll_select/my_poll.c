#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/poll.h>

#include <linux/gpio.h>
#include <linux/interrupt.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Poll demo - character device with GPIO interrupt");

#define BUTTON_GPIO 20
#define IO_OFFSET 512
static int button_gpio = (BUTTON_GPIO + IO_OFFSET);

static const char       *my_device = "my_cdev";

static dev_t            dev_nr;
static struct cdev      my_cdev;
static struct class     *my_class;

/* Variable contains irq number */
unsigned int irq_number;


static unsigned int event_count = 0;
static char message[] = "Hello from Kernel!";

/* Static declaration */
static DECLARE_WAIT_QUEUE_HEAD(my_wait_queue); /* name fre wait queue */

/* Or dynamic */
// wait_queue_head_t my_queue;
// init_waitqueue_head(&my_queue);

/* ------------------------------------------------------------------ */
/*  Per-open-instance state                                             */
/* ------------------------------------------------------------------ */
struct my_dev_data {
    unsigned int last_event;  /* Value of event_count at last read */
};

/* ------------------------------------------------------------------ */
/*  File operations                                                   */
/* ------------------------------------------------------------------ */

static ssize_t my_read(struct file *file,
                       char __user *buf,
                       size_t len,
                       loff_t *off)
{
    pr_info("%s: read called\n", my_device);

    struct my_dev_data *data = file->private_data;

    if (data->last_event == event_count) {

        /* Non-blocking: return immediately with -EAGAIN */
        if (file->f_flags & O_NONBLOCK) {
            pr_info("%s: non-blocking mode - no data\n", my_device);
            return -EAGAIN;
        }

       /* Blocking: sleep until data is available or a signal arrives */
        pr_info("%s: blocking mode - sleeping\n", my_device);

        int ret = wait_event_interruptible(my_wait_queue, data->last_event != event_count);
        if (ret)
            return -ERESTARTSYS;  /* Interrupted by a signal - propagate to user space */

    }

    /* Consume the event : mark this fd as up to date */
    data->last_event = event_count;

    if (copy_to_user(buf, message, sizeof(message)))
        return -EFAULT;

    return sizeof(message);
}

static __poll_t my_poll(struct file *file, struct poll_table_struct *poll_table){
    pr_info("%s: poll called\n", my_device);

    struct my_dev_data *data = file->private_data;

    __poll_t mask = 0;

    /* Register our wait queue - does NOT sleep */
    poll_wait(file, &my_wait_queue, poll_table);

    if(data->last_event != event_count)
        mask |= EPOLLIN | EPOLLRDNORM;

    return mask;
}

static int my_open(struct inode *inode, struct file *file)
{
    struct my_dev_data *data = kmalloc(sizeof(struct my_dev_data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    /* Sync to current event count so this fd only sees future events */
    data->last_event = event_count;
    file->private_data = data;

    pr_info("%s: device opened\n", my_device);
    return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);

    pr_info("%s: device closed\n", my_device);
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = my_open,
    .read = my_read,
    .poll = my_poll,
    .release = my_release,

};


/* ------------------------------------------------------------------ */
/*  GPIO Interrupt Service Routine                                    */
/* ------------------------------------------------------------------ */

static irqreturn_t isr(int irq, void *dev_id){
    pr_info("%s: GPIO Interrupt occoured\n", my_device);
    
    event_count++;                            /* New event - all fds will see this */

    /* Wake up waiting processes */
    wake_up_interruptible(&my_wait_queue);
    
    return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/*  GPIO / IRQ setup                                                  */
/* ------------------------------------------------------------------ */

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