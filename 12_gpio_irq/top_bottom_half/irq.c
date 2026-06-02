#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("GPIO top bottom half of IRQ Example");

static const char *device_name = "gpio_ctrl";

#define LED_GPIO 21
#define BUTTON_GPIO 20

#define GPIO_OFFSET 512

static int led_gpio = (LED_GPIO + GPIO_OFFSET);
static int button_gpio = (BUTTON_GPIO + GPIO_OFFSET);

/* Variable contains iqr number */
unsigned int irq_number;

static struct work_struct button_work;

/* Top-half GPIO ISR */
static irqreturn_t button_isr(int irq, void *dev_id){
    pr_info("%s: Interrupt occoured on GPIO 20\n", device_name);
    
    schedule_work(&button_work);
    
    return IRQ_HANDLED;
}


/* Bottom-half (Workqueue) */
static void button_work_handler(struct work_struct *work){
    pr_info("%s: Bottom-half: processing button press\n", device_name);

    /* simulate slow work */
    msleep(1000);

    gpio_set_value(led_gpio, !gpio_get_value(led_gpio));
}

static int __init my_init(void) {
    int status;
    status = gpio_request(led_gpio, "led_gpio");
    if(status){
        pr_err("%s: Failed to request led gpio 21\n", device_name);
        return -status;
    }

    status = gpio_direction_output(led_gpio, 0);
    if(status){
        pr_err("%s: Failed to set gpio 20 led direction\n", device_name);
        gpio_free(led_gpio);
        return -status;
    }

    status = gpio_request(button_gpio, "button_gpio");
    if(status){
        pr_err("%s: Failed to request button gpio 20\n", device_name);
        gpio_free(led_gpio);
        return -status;
    }

    status = gpio_direction_input(button_gpio);
    if(status){
        pr_err("%s: Failed to set gpio 21 button direction\n", device_name);
        gpio_free(led_gpio);
        gpio_free(button_gpio);
        return -status;
    }

    irq_number = gpio_to_irq(button_gpio);

    if(irq_number < 0){
        pr_err("%s: Failed to get IRQ number for the button GPIO %d\n", device_name, irq_number);
        gpio_free(led_gpio);
        gpio_free(button_gpio);
        return irq_number;
    }

    status = request_irq(irq_number, button_isr, IRQF_TRIGGER_FALLING, "btn_irq_handler", NULL);
    if(status){
        pr_err("%s: IRQ request failed\n", device_name);
        gpio_free(led_gpio);
        gpio_free(button_gpio);
        return -status;
    }
    pr_info("%s: irq_number=%d \n", device_name, irq_number);

    INIT_WORK(&button_work, button_work_handler);

    pr_info("%s: GPIO request example loaded\n", device_name);

    return 0;
}

static void __exit my_exit(void){
    gpio_set_value(led_gpio, 0);
    gpio_free(led_gpio);
    gpio_free(button_gpio);
    free_irq(irq_number, NULL);

    cancel_work_sync(&button_work);

    pr_info("%s: Goodbye Kernel\n", device_name);
}

module_init(my_init);
module_exit(my_exit);