#define USE_DESCRIPTOR_API 

#include <linux/module.h>
#include <linux/init.h>

#ifdef USE_DESCRIPTOR_API 
#include <linux/gpio/consumer.h>
#else
#include <linux/gpio.h> // legacy
#endif

#include <linux/interrupt.h>
#include <linux/timer_types.h> 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("GPIO IRQ Example");

static const char *device_name = "gpio_ctrl";

#define LED_GPIO 21
#define BUTTON_GPIO 20

#define GPIO_OFFSET 512

static int led_gpio = (LED_GPIO + GPIO_OFFSET);
static int button_gpio = (BUTTON_GPIO + GPIO_OFFSET);

#ifdef USE_DESCRIPTOR_API
static struct gpio_desc *led, *button;
#endif

/* Variable contains iqr number */
unsigned int irq_number;

/* debounsing */
#define DEBOUNCE_DELAY 20 // 20ms

static int last_button_state;
static struct timer_list debounce_timer;

/* GPIO ISR */
static irqreturn_t button_isr(int irq, void *dev_id){
    pr_info("%s: Interrupt occoured on GPIO 20\n", device_name);

#ifdef USE_DESCRIPTOR_API
        last_button_state = gpiod_get_value(button);
#else
        last_button_state = gpio_get_value(button_gpio);
#endif
    
    mod_timer(&debounce_timer, jiffies + msecs_to_jiffies(DEBOUNCE_DELAY));
    
    return IRQ_HANDLED;
}

static void debounce_timer_callback(struct timer_list *t){
#ifdef USE_DESCRIPTOR_API
    int state = gpiod_get_value(button);
#else
    int state = gpio_get_value(button_gpio);
#endif
    if(state == last_button_state){
        pr_info("%s: Valid button press detected\n", device_name);
#ifdef USE_DESCRIPTOR_API
        gpiod_set_value(led, !gpiod_get_value(led));
#else
        gpio_set_value(led_gpio, !gpio_get_value(led_gpio));
#endif
    }
}

static int __init my_init(void) {
    int status;

#ifdef USE_DESCRIPTOR_API
    led = gpio_to_desc(led_gpio);
    if(!led){
		pr_err("%s: unable gpio_to_desc led_gpio 21\n", device_name);
		return -1;
	}

    button = gpio_to_desc(button_gpio);
	if(!button){
		pr_err("%s: unable gpio_to_desc button_gpio 20\n", device_name);
		return -1;
	}

    status = gpiod_direction_output(led, 0);
    if(status){
        pr_err("%s: Failed to set gpio 20 led direction\n", device_name);
        return -status;
    }

    status = gpiod_direction_input(button);
    if(status){
        pr_err("%s: Failed to set gpio 21 button direction\n", device_name);
        return -status;
    }

#else
    status = gpio_request(led_gpio, "led_gpio");
    if(status){
        pr_err("%s: Failed to request led gpio 21\n", device_name);
        return -status;
    }

    status = gpio_request(button_gpio, "button_gpio");
    if(status){
        pr_err("%s: Failed to request button gpio 20\n", device_name);
        gpio_free(led_gpio);
        return -status;
    }

    status = gpio_direction_output(led_gpio, 0);
    if(status){
        pr_err("%s: Failed to set gpio 20 led direction\n", device_name);
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
#endif
      
#ifdef USE_DESCRIPTOR_API
    irq_number = gpiod_to_irq(button);
    if(irq_number < 0){
        pr_err("%s: Failed to get IRQ number for the button GPIO %d\n", device_name, irq_number);
        return irq_number;
    }
#else
    irq_number = gpio_to_irq(button_gpio);
    if(irq_number < 0){
        pr_err("%s: Failed to get IRQ number for the button GPIO %d\n", device_name, irq_number);
        gpio_free(led_gpio);
        gpio_free(button_gpio);
        return irq_number;
    }
#endif

    status = request_irq(irq_number, button_isr, IRQF_TRIGGER_FALLING, "btn_irq_handler", NULL);
    if(status){
        pr_err("%s: IRQ request failed\n", device_name);

#ifndef USE_DESCRIPTOR_API
        gpio_free(led_gpio);
        gpio_free(button_gpio);
 #endif

        return -status;
    }
    pr_info("%s: irq_number=%d \n", device_name, irq_number);

    timer_setup(&debounce_timer, debounce_timer_callback, 0);

    pr_info("%s: GPIO request example loaded\n", device_name);

    return 0;
}

static void __exit my_exit(void){
#ifdef USE_DESCRIPTOR_API
    gpiod_set_value(led, 0);
#else  
    gpio_set_value(led_gpio, 0);
    gpio_free(led_gpio);
    gpio_free(button_gpio);
#endif
    free_irq(irq_number, NULL);
    del_timer_sync(&debounce_timer);
    pr_info("%s: Goodbye Kernel\n", device_name);
}

module_init(my_init);
module_exit(my_exit);