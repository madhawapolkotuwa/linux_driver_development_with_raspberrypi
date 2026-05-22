#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>    /* tasklet_struct */
#include <linux/atomic.h>
#include <linux/delay.h>        /* msleep, mdelay */
#include <linux/jiffies.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Tasklet vs Workqueue Comparison");

#define DRIVER_NAME   "tasklet_vs_wq"


#define GPIO_OFFSET 512

#define GPIO_BUTTON   20  + GPIO_OFFSET   /* input  -> falling edge triggers IRQ  */
#define GPIO_LED      21  + GPIO_OFFSET   /* output -> controlled by bottom half  */

#define LED_DELAY_MS  500       /* delay before LED ON  (workqueue only) */
#define LED_ON_MS    1000       /* how long LED stays on                 */


/* ------------------------------------------------------------------ */
/*  Module parameter                                                    */
/* ------------------------------------------------------------------ */

static int mode = 2;
module_param(mode, int, 0444);
MODULE_PARM_DESC(mode, "Bottom-half mode: 1=tasklet  2=workqueue");

/* ------------------------------------------------------------------ */
/*  Shared state                                                        */
/* ------------------------------------------------------------------ */

static int             irq_number;
static atomic_t        press_count  = ATOMIC_INIT(0);
static unsigned long   irq_jiffies;

/* ------------------------------------------------------------------ */
/*  MODE 1 - TASKLET bottom-half                                        */
/*                                                                      */
/*  A tasklet runs in SOFTIRQ context - one step above hardirq but     */
/*  still ATOMIC. Rules:                                                */
/*    ❌ msleep() is FORBIDDEN - will trigger "scheduling while atomic" */
/*    ❌ mutex_lock() is FORBIDDEN - mutex can sleep                    */
/*    ✅ gpio_set_value() is OK (non-sleeping)                          */
/*    ✅ atomic operations are OK                                       */
/*    ✅ mdelay() is OK - but it busy-waits, burning the CPU            */
/*                                                                      */
/*  Because we cannot sleep, we cannot add a clean delay before        */
/*  turning the LED on. The best we can do is a CPU-burning mdelay()   */
/*  for very short waits - completely impractical for 500ms.           */
/* ------------------------------------------------------------------ */

static void tasklet_led_handler(struct tasklet_struct *t)
{
    unsigned int press = atomic_read(&press_count);

    pr_info("[%s] TASKLET (softirq ctx) | press #%u | in_interrupt()=%lu\n",
            DRIVER_NAME, press, in_interrupt());

    /*
     * ---------------------------------------------------------------
     * KEY POINT - Try uncommenting the msleep() below and reload:
     *   sudo insmod my_driver.ko mode=1
     *   Press the button → kernel will immediately BUG():
     *   "BUG: scheduling while atomic: ..."
     *
     * This is WHY tasklets cannot do anything that sleeps.
     * ---------------------------------------------------------------
     */
    /* msleep(LED_DELAY_MS); */  /* <-- UNCOMMENT TO SEE THE BUG! */

    /*
     * No delay is possible here without busy-waiting.
     * mdelay() busy-waits (holds the CPU) - acceptable only for
     * microsecond-range delays, never for hundreds of milliseconds.
     */
    pr_info("[%s] TASKLET: Cannot msleep() - turning LED ON instantly\n",
            DRIVER_NAME);

    gpio_set_value(GPIO_LED, 1);
    pr_info("[%s] TASKLET: LED ON\n", DRIVER_NAME);

    /*
     * We also cannot sleep for LED_ON_MS here.
     * A real driver would schedule a hrtimer for the OFF, which adds
     * significant complexity - further proof workqueues are simpler.
     */
    mdelay(50);   /* tiny busy-wait just to make LED flash visible */

    gpio_set_value(GPIO_LED, 0);
    pr_info("[%s] TASKLET: LED OFF (no clean delay was possible)\n",
            DRIVER_NAME);
}


/* Declare the tasklet - DECLARE_TASKLET_OLD removed in 5.x,
 * use tasklet_setup / DECLARE_TASKLET with new API               */
static DECLARE_TASKLET(led_tasklet, tasklet_led_handler);


/* ------------------------------------------------------------------ */
/*  MODE 2 - WORKQUEUE bottom-half                                      */
/*                                                                      */
/*  The work handler runs in PROCESS context (kworker thread).         */
/*  Rules:                                                              */
/*    ✅ msleep() allowed - we can cleanly delay 500ms                 */
/*    ✅ mutex_lock() allowed                                           */
/*    ✅ I2C / SPI transactions allowed                                 */
/*    ✅ kmalloc(GFP_KERNEL) allowed                                    */
/* ------------------------------------------------------------------ */

static struct work_struct led_work;

static void wq_led_handler(struct work_struct *work)
{
    unsigned int press    = atomic_read(&press_count);
    unsigned long latency = jiffies - irq_jiffies;

    pr_info("[%s] WORKQUEUE (process ctx) | press #%u | "
            "IRQ→WQ latency ~%u ms | in_interrupt()=%lu\n",
            DRIVER_NAME, press,
            jiffies_to_msecs(latency),
            in_interrupt());          /* will print 0 - we are NOT atomic */

    /*
     * ---------------------------------------------------------------
     * KEY POINT - This msleep() is perfectly safe here.
     * Compare with the tasklet handler above where it would BUG.
     * in_interrupt() returns 0 here, confirming process context.
     * ---------------------------------------------------------------
     */
    pr_info("[%s] WORKQUEUE: msleep(%d) sleeping cleanly...\n",
            DRIVER_NAME, LED_DELAY_MS);
    msleep(LED_DELAY_MS);   /* ✅ safe - process context */

    gpio_set_value(GPIO_LED, 1);
    pr_info("[%s] WORKQUEUE: LED ON after %d ms delay\n",
            DRIVER_NAME, LED_DELAY_MS);

    msleep(LED_ON_MS);      /* ✅ safe - keep LED on for 1 second */

    gpio_set_value(GPIO_LED, 0);
    pr_info("[%s] WORKQUEUE: LED OFF clean delay was possible!\n",
            DRIVER_NAME);
}


/* ------------------------------------------------------------------ */
/*  Top half - shared ISR for both modes                               */
/*                                                                      */
/*  Regardless of mode, the ISR is minimal:                            */
/*    1. Record timestamp                                               */
/*    2. Increment press counter                                        */
/*    3. Schedule the selected bottom-half                              */
/*    4. Return immediately                                             */
/* ------------------------------------------------------------------ */

static irqreturn_t button_isr(int irq, void *dev_id)
{
    irq_jiffies = jiffies;
    atomic_inc(&press_count);

    pr_info("[%s] TOP HALF (hardirq ctx): press #%u - "
            "scheduling %s bottom-half\n",
            DRIVER_NAME,
            atomic_read(&press_count),
            (mode == 1) ? "TASKLET" : "WORKQUEUE");

    if (mode == 1) {
        /* Tasklet: runs in softirq context after ISR returns */
        tasklet_schedule(&led_tasklet);
    } else {
        /* Workqueue: runs in process context (kworker thread) */
        schedule_work(&led_work);
    }

    return IRQ_HANDLED;
}


/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static int __init my_init(void)
{
    int ret;

    if (mode != 1 && mode != 2) {
        pr_err("[%s] Invalid mode=%d. Use mode=1 (tasklet) or mode=2 (workqueue)\n",
               DRIVER_NAME, mode);
        return -EINVAL;
    }

    pr_info("[%s] Loading\n", DRIVER_NAME);
    pr_info("[%s] Mode: %d (%s)\n",
            DRIVER_NAME, mode,
            (mode == 1) ? "TASKLET  - softirq context, NO sleep"
                        : "WORKQUEUE - process context, sleep OK");

    /* ---- GPIO: Button -------------------------------------------- */
    ret = gpio_request(GPIO_BUTTON, "button_gpio");
    if (ret) {
        pr_err("[%s] gpio_request(%d) failed: %d\n",
               DRIVER_NAME, GPIO_BUTTON, ret);
        return ret;
    }
    gpio_direction_input(GPIO_BUTTON);

    /* ---- GPIO: LED ----------------------------------------------- */
    ret = gpio_request(GPIO_LED, "led_gpio");
    if (ret) {
        pr_err("[%s] gpio_request(%d) failed: %d\n",
               DRIVER_NAME, GPIO_LED, ret);
        goto err_gpio_button;
    }
    gpio_direction_output(GPIO_LED, 0);

    /* ---- Init the selected bottom-half mechanism ----------------- */
    if (mode == 2)
        INIT_WORK(&led_work, wq_led_handler);
    /* Tasklet was declared statically with DECLARE_TASKLET above    */

    /* ---- IRQ ----------------------------------------------------- */
    irq_number = gpio_to_irq(GPIO_BUTTON);
    if (irq_number < 0) {
        pr_err("[%s] gpio_to_irq failed: %d\n", DRIVER_NAME, irq_number);
        ret = irq_number;
        goto err_gpio_led;
    }

    ret = request_irq(irq_number,
                      button_isr,
                      IRQF_TRIGGER_FALLING,
                      DRIVER_NAME,
                      NULL);
    if (ret) {
        pr_err("[%s] request_irq failed: %d\n", DRIVER_NAME, ret);
        goto err_gpio_led;
    }

    pr_info("[%s] Ready! GPIO%d=button | GPIO%d=LED | IRQ=%d\n",
            DRIVER_NAME, GPIO_BUTTON, GPIO_LED, irq_number);

    if (mode == 1) {
        pr_info("[%s] Press button → tasklet fires instantly (no delay)\n",
                DRIVER_NAME);
        pr_info("[%s] TIP: Uncomment msleep() in tasklet handler,\n",
                DRIVER_NAME);
        pr_info("[%s]      rebuild, and press button to see the BUG!\n",
                DRIVER_NAME);
    } else {
        pr_info("[%s] Press button → LED turns on after %d ms delay\n",
                DRIVER_NAME, LED_DELAY_MS);
    }

    return 0;

err_gpio_led:
    gpio_free(GPIO_LED);
err_gpio_button:
    gpio_free(GPIO_BUTTON);
    return ret;
}

static void __exit my_exit(void)
{
    pr_info("[%s] Unloading (mode=%d)...\n", DRIVER_NAME, mode);

    /* 1. Disable further interrupts */
    free_irq(irq_number, NULL);

    /* 2. Stop the bottom-half - method depends on mode */
    if (mode == 1) {
        /* tasklet_kill() waits if the tasklet is currently running.
         * Safe equivalent of cancel_work_sync() for tasklets.        */
        tasklet_kill(&led_tasklet);
    } else {
        /* cancel_work_sync() waits if work handler is running.
         * Without this, gpio_free() below could race with the
         * handler still calling gpio_set_value().                    */
        cancel_work_sync(&led_work);
    }

    /* 3. Release GPIOs */
    gpio_set_value(GPIO_LED, 0);
    gpio_free(GPIO_LED);
    gpio_free(GPIO_BUTTON);

    pr_info("[%s] Total presses: %d | Unloaded.\n",
            DRIVER_NAME, atomic_read(&press_count));
}

module_init(my_init);
module_exit(my_exit);






