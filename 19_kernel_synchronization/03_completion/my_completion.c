#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kthread.h>

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

#include <linux/completion.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Kernel Synchronization: completion");

static const char *my_device = "my_completion";

static DECLARE_COMPLETION(my_comp);

static struct task_struct *waiter_thread_1;
static struct task_struct *waiter_thread_2;

/* GPIO Button */
#define BUTTON_GPIO 20
#define IO_OFFSET 512
static int button_gpio = (BUTTON_GPIO + IO_OFFSET);


static struct gpio_desc *button; /* GPIO descriptor */
/* Variable contains irq number */
unsigned int irq_number;

static int press_count = 0;

/* thread function */

static int waiter_thread_fn_1(void *data)
{
    pr_info("%s: waiter_thread_1 started - calling wait_for_completion()\n",
            my_device);
 
    while (!kthread_should_stop()) {
 
        pr_info("%s: waiter_thread_1 sleeping - waiting for button press\n",
                my_device);
 
        /*
        * The thread sleeps here.
        * It wakes up only when the ISR calls complete().
        * wait_for_completion_interruptible() is used instead of
        * wait_for_completion() so that kthread_stop() during rmmod
        * can unblock this call cleanly.
        * If wait_for_completion() were used, rmmod would hang
        * forever waiting for a button press that never comes.
        */
        if (wait_for_completion_interruptible(&my_comp) != 0) {
            pr_info("%s: waiter_thread_1 interrupted - exiting\n", my_device);
            break;
        }
 
        /* Woke up - ISR called complete() */
        pr_info("%s: waiter_thread_1 WOKE UP - processing press #%d\n",
                my_device, press_count);
 
        reinit_completion(&my_comp);
    }
 
    return 0;
}

static int waiter_thread_fn_2(void *data)
{
    pr_info("%s: waiter_thread_2 started - calling wait_for_completion()\n",
            my_device);
 
    while (!kthread_should_stop()) {
 
        pr_info("%s: waiter_thread_2 sleeping - waiting for button press\n",
                my_device);
 
        if (wait_for_completion_interruptible(&my_comp) != 0) {
            pr_info("%s: waiter_thread_2 interrupted - exiting\n", my_device);
            break;
        }
 
        /* Woke up - ISR called complete() */
        pr_info("%s: waiter_thread_2 WOKE UP - processing press #%d\n",
                my_device, press_count);
 
        reinit_completion(&my_comp);
    }
 
    return 0;
}


#define DEBOUNCE_MS 20 // 20ms

static unsigned long last_jiffies = 0;

/* ------------------------------------------------------------------ */
/*  GPIO Interrupt Service Routine                                    */
/* ------------------------------------------------------------------ */

static irqreturn_t isr(int irq, void *dev_id){
    pr_info("%s: GPIO Interrupt occoured\n", my_device);

    if(jiffies - last_jiffies < msecs_to_jiffies(DEBOUNCE_MS))
        return IRQ_HANDLED;

    last_jiffies = jiffies;

    press_count++;
 
    pr_info("%s: ISR fired (press #%d) - calling complete()\n", my_device, press_count);

    /*
     * Signal the completion.
     * comp_waiter is sleeping inside wait_for_completion().
     * complete() increments the internal counter and wakes it up.
     * complete() is safe in interrupt context - it never sleeps.
     */
    complete(&my_comp);

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

    status = external_gpio_irq_setup(button_gpio);
    if(status) { // set gpio 20 as exteranl gpio interrupt
       pr_err("%s: GPIO %d failed to set up as external interrupt\n", my_device, button_gpio);
       return status;
    }

    waiter_thread_1 = kthread_run(waiter_thread_fn_1, NULL, "comp_waite_1");
    if(IS_ERR(waiter_thread_1)){
        status = PTR_ERR(waiter_thread_1);
        pr_err("%s: kthread_run faild 1: %d\n",my_device, status);
        free_irq(irq_number, NULL);
        return status;
    }

    waiter_thread_2 = kthread_run(waiter_thread_fn_2, NULL, "comp_waite_2");
    if(IS_ERR(waiter_thread_2)){
        status = PTR_ERR(waiter_thread_2);
        pr_err("%s: kthread_run faild 2: %d\n",my_device, status);
        free_irq(irq_number, NULL);
        return status;
    }

    pr_info("%s: device initilized\n", my_device);
    return 0;
}

static void __exit my_exit(void){
    
    kthread_stop(waiter_thread_1);
    kthread_stop(waiter_thread_2);

    free_irq(irq_number, NULL);

    pr_info("%s: driver unloaded - total presses: %d\n", my_device, press_count);
}

module_init(my_init);
module_exit(my_exit);





