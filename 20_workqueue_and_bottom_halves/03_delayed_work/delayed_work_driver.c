#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/delay.h>        /* msleep() - only safe in process context! */
#include <linux/jiffies.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Delayed Work Self-Rescheduling Example");


#define DRIVER_NAME  "delayed_work_demo"

/* ------------------------------------------------------------------ */
/*  Module parameter                                                    */
/* ------------------------------------------------------------------ */

static unsigned int period_ms = 1000; // 1s
module_param(period_ms, uint, 0444);
MODULE_PARM_DESC(period_ms, "Period between work executions in ms (default: 1000)");


/* ------------------------------------------------------------------ */
/*  Delayed work                                                        */
/* ------------------------------------------------------------------ */

static struct delayed_work periodic_work;
static atomic_t tick_count = ATOMIC_INIT(0);

/* Forward declare so handler can reschedule itself */
static void periodic_work_handler(struct work_struct *work);

static void periodic_work_handler(struct work_struct *work)
{
    unsigned int tick = atomic_inc_return(&tick_count);

    pr_info("[%s] Tick #%u | jiffies=%lu | PID=%d | CPU=%d\n",
            DRIVER_NAME, tick, jiffies, current->pid, smp_processor_id());

    /* Demonstrate process-context privilege: sleeping is allowed */
    /* (In a real driver this could be an I2C read, file write, etc.) */
    msleep(100);   

    /* Self-reschedule: queue ourselves again after period_ms.
     *
     * container_of() recovers our delayed_work from the work_struct.
     * msecs_to_jiffies() converts milliseconds to kernel timer ticks.
     *
     * NOTE: We check if the module is being removed before rescheduling
     * to avoid queuing work after destroy - see exit function.        */
    queue_delayed_work(system_wq,
                       container_of(work, struct delayed_work, work),
                       msecs_to_jiffies(period_ms));
}

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static int __init my_init(void)
{
    pr_info("[%s] Loading\n", DRIVER_NAME);
    pr_info("[%s] Period = %u ms  (%lu jiffies)\n",
            DRIVER_NAME, period_ms, msecs_to_jiffies(period_ms));

    /* INIT_DELAYED_WORK binds the delayed_work to its handler */
    INIT_DELAYED_WORK(&periodic_work, periodic_work_handler);

    /* Schedule the first execution after one period */
    queue_delayed_work(system_wq, &periodic_work,
                       msecs_to_jiffies(period_ms));

    pr_info("[%s] First execution scheduled in %u ms\n",
            DRIVER_NAME, period_ms);
    return 0;
}

static void __exit my_exit(void)
{
    pr_info("[%s] Unloading - cancelling delayed work...\n", DRIVER_NAME);

    /* cancel_delayed_work_sync():
     *   - Cancels the pending work if it has not started yet, OR
     *   - Waits for it to finish if it is currently running.
     *   - After this returns, the handler will NOT run again.
     *
     * This is the safe way to stop a self-rescheduling work item.
     * Never call just cancel_delayed_work() without _sync on exit -
     * a race between cancellation and the running handler can corrupt
     * module memory after rmmod.                                      */
    cancel_delayed_work_sync(&periodic_work);

    pr_info("[%s] Executed %d ticks total. Unloaded.\n",
            DRIVER_NAME, atomic_read(&tick_count));
}

module_init(my_init);
module_exit(my_exit);






