#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "gpio_led_ctrl"
#define CLASS_NAME  "gpio_class"

/* Private structure to hold driver data */
struct gpio_dt_priv {
    struct gpio_desc *led;
    struct gpio_desc *button;
    int led_state;
    unsigned int irq_number;
    dev_t dev_num;
    struct cdev c_dev;
    struct class *cl;
};

/* --- File Operations for Terminal Control --- */

static int dev_open(struct inode *inod, struct file *fil)
{
    /* Store the private structure into the file's private data for use in read/write */
    struct gpio_dt_priv *priv = container_of(inod->i_cdev, struct gpio_dt_priv, c_dev);
    fil->private_data = priv;
    return 0;
}

static ssize_t dev_read(struct file *fil, char __user *buf, size_t len, loff_t *off)
{
    struct gpio_dt_priv *priv = fil->private_data;
    char buffer[14];
    int size;

    if (*off > 0)
        return 0; /* EOF */

    size = snprintf(buffer, sizeof(buffer), "LED state: %d\n", priv->led_state);
    
    if (copy_to_user(buf, buffer, size))
        return -EFAULT;

    *off += size;
    return size;
}

static ssize_t dev_write(struct file *fil, const char __user *buf, size_t len, loff_t *off)
{
    struct gpio_dt_priv *priv = fil->private_data;
    char k_buf[4] = {0};

    if (len > sizeof(k_buf) - 1)
        return -EINVAL;

    if (copy_from_user(k_buf, buf, len))
        return -EFAULT;

    if (k_buf[0] == '1') {
        priv->led_state = 1;
        gpiod_set_value(priv->led, 1);
        pr_info("LED toggled via terminal: ON\n");
    } else if (k_buf[0] == '0') {
        priv->led_state = 0;
        gpiod_set_value(priv->led, 0);
        pr_info("LED toggled via terminal: OFF\n");
    }

    return len;
}

static int dev_release(struct inode *inod, struct file *fil)
{
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

/* --- Interrupt Service Routine (ISR) --- */
static irqreturn_t button_isr(int irq, void *dev)
{
    /* dev_id contains our passed priv structure pointer */
    struct gpio_dt_priv *priv = (struct gpio_dt_priv *)dev;
    
    if (!priv)
        return IRQ_NONE;

    /* Toggle the LED state */
    priv->led_state = !priv->led_state;
    gpiod_set_value(priv->led, priv->led_state);
    
    pr_info("Button Interrupted! LED is now %s\n", priv->led_state ? "ON" : "OFF");

    return IRQ_HANDLED;
}

/* --- Probe and Remove --- */
static int gpio_dt_probe(struct platform_device *pdev)
{
    int val;
    struct gpio_dt_priv *priv;

    dev_info(&pdev->dev, "gpio_dt_driver probed\n");

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    /* Request the LED GPIO (output) */
    priv->led = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(priv->led)) {
        dev_err(&pdev->dev, "Failed to get LED GPIO: %ld\n", PTR_ERR(priv->led));
        return PTR_ERR(priv->led);
    }

    /* Request the button GPIO (input) */
    priv->button = devm_gpiod_get(&pdev->dev, "button", GPIOD_IN);
    if (IS_ERR(priv->button)) {
        dev_err(&pdev->dev, "Failed to get button GPIO: %ld\n", PTR_ERR(priv->button));
        return PTR_ERR(priv->button);
    }

    /* Read initial button state */
    val = gpiod_get_value(priv->button);
    dev_info(&pdev->dev, "Initial Button state: %d\n", val);

    /* GPIO IRQ Mapping */
    val = gpiod_to_irq(priv->button);
    if (val < 0) {
        dev_err(&pdev->dev, "Failed to get IRQ number for the button GPIO %d\n", val);
        return val;
    }
    priv->irq_number = val;

    /* Pass 'priv' as last parameter */
    val = request_irq(priv->irq_number, button_isr, IRQF_TRIGGER_FALLING, "btn_irq_handler", priv);
    if (val) {
        dev_err(&pdev->dev, "IRQ request failed\n");
        return val;
    }

    priv->led_state = 0;

    /* --- Dynamic Character Device Registration --- */
    
    if (alloc_chrdev_region(&priv->dev_num, 0, 1, DEVICE_NAME) < 0) {
        dev_err(&pdev->dev, "Failed to allocate chrdev region\n");
        goto free_irq;
    }

    cdev_init(&priv->c_dev, &fops);
    if (cdev_add(&priv->c_dev, priv->dev_num, 1) < 0) {
        dev_err(&pdev->dev, "Failed to add cdev\n");
        goto unregister_chrdev;
    }

    priv->cl = class_create(CLASS_NAME);
    if (IS_ERR(priv->cl)) {
        dev_err(&pdev->dev, "Failed to create class\n");
        goto del_cdev;
    }

    if (IS_ERR(device_create(priv->cl, NULL, priv->dev_num, NULL, DEVICE_NAME))) {
        dev_err(&pdev->dev, "Failed to create device file\n");
        goto destroy_class;
    }

    platform_set_drvdata(pdev, priv);

    dev_info(&pdev->dev, "gpio_dt_driver probe exit success\n");
    return 0;

/* Error Handling Labels */
destroy_class:
    class_destroy(priv->cl);
del_cdev:
    cdev_del(&priv->c_dev);
unregister_chrdev:
    unregister_chrdev_region(priv->dev_num, 1);
free_irq:
    free_irq(priv->irq_number, priv);
    return -1;
}

static void gpio_dt_remove(struct platform_device *pdev)
{
    struct gpio_dt_priv *priv = platform_get_drvdata(pdev);

    /* Cleanup character device details */
    device_destroy(priv->cl, priv->dev_num);
    class_destroy(priv->cl);
    cdev_del(&priv->c_dev);
    unregister_chrdev_region(priv->dev_num, 1);

    /* Free the manually requested IRQ safely passing the priv dev_id */
    free_irq(priv->irq_number, priv);

    /* Turn the LED off before unloading */
    gpiod_set_value(priv->led, 0);
    dev_info(&pdev->dev, "LED turned OFF and driver removed cleanly\n");
}

static struct of_device_id gpio_dt_of_match[] = {
    { .compatible = "testdevice,mydev" },
    { }
};
MODULE_DEVICE_TABLE(of, gpio_dt_of_match);

static struct platform_driver gpio_dt_driver = {
    .probe  = gpio_dt_probe,
    .remove = gpio_dt_remove,
    .driver = {
        .name           = "gpio_dt_driver",
        .of_match_table = gpio_dt_of_match,
    },
};

module_platform_driver(gpio_dt_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Platform driver demonstrating GPIO from Device Tree");
