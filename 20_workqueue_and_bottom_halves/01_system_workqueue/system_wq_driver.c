#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/delay.h>        /* msleep() - only safe in process context! */
#include <linux/slab.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("System Workqueue Basic Example");

#define DRIVER_NAME  "system_wq"

static atomic_t trigger_count = ATOMIC_INIT(0);

/*
 * system_work_handler - the bottom-half work handler
 *
 * Runs in process context inside a kworker thread from system_wq.
 * We do not create or manage any thread - the kernel handles it.
 *
 * Safe to do here:
 *   msleep()            ✅  process context allows sleeping
 *   kmalloc(GFP_KERNEL) ✅  process context allows sleeping alloc
 *   mutex_lock()        ✅  process context allows blocking
 */
static void system_work_handler(struct work_struct *work)
{
    unsigned int count = atomic_read(&trigger_count);

    pr_info("[%s] Work handler running | trigger #%u | PID=%d | CPU=%d\n",
            DRIVER_NAME, count, current->pid, smp_processor_id());

    pr_info("[%s] Simulating deferred work - sleeping 1000ms...\n",
            DRIVER_NAME);

    /*
     * This msleep() represents slow work that cannot run in an ISR:
     * reading a sensor, writing to storage, waiting for a bus, etc.
     * It is safe here because we are in a process context.
     */        
    msleep(2000);

    pr_info("[%s] Deferred work done for trigger #%u\n",
            DRIVER_NAME, count);
}

/*
 * DECLARE_WORK - static work item declaration.
 *
 * Binds the work_struct 'my_work' to 'system_work_handler'.
 * schedule_work() returns false if work is already pending -
 * preventing duplicate jobs on rapid back-to-back triggers.
 */
static DECLARE_WORK(my_work, system_work_handler);


static struct kobject *system_wq_kobj;


/*
 * trigger_store - called when user writes to
 *                 /sys/kernel/system_wq/trigger
 *
 * In a real hardware driver, this schedule_work() call would live
 * inside the ISR. Here we use sysfs so you can trigger it manually
 * from the terminal without needing physical hardware.
 *
 * schedule_work() is safe to call from ANY context - including
 * hardirq - which is what makes it useful inside an ISR.
 */
static ssize_t trigger_store(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             const char *buf, size_t count)
{
    int val;

    if (kstrtoint(buf, 10, &val) != 0)
        return -EINVAL;

    if (val == 1) {
        atomic_inc(&trigger_count);
 
        pr_info("[%s] Trigger #%u received - scheduling work on system_wq\n",
                DRIVER_NAME, atomic_read(&trigger_count));
 
        /*
         * schedule_work() queues my_work onto the kernel's shared
         * system_wq. No thread management needed - the kernel handles
         * picking a kworker thread to run the handler.
         *
         * Returns true  → work queued successfully
         * Returns false → work already pending, no duplicate queued
         */
        if (!schedule_work(&my_work))
            pr_info("[%s] Work already pending - skipped duplicate\n",
                    DRIVER_NAME);
    }

    return count;
}

static struct kobj_attribute trigger_attr =
    __ATTR(trigger, 0220, NULL, trigger_store);  /* write-only */

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static int __init my_init(void)
{

    int ret;
 
    pr_info("[%s] Loading...\n", DRIVER_NAME);
 
    /* Create /sys/kernel/system_wq/ directory */
    system_wq_kobj = kobject_create_and_add(DRIVER_NAME, kernel_kobj);
    if (!system_wq_kobj) {
        pr_err("[%s] Failed to create kobject\n", DRIVER_NAME);
        return -ENOMEM;
    }
 
    /* Create /sys/kernel/system_wq/trigger file */
    ret = sysfs_create_file(system_wq_kobj, &trigger_attr.attr);
    if (ret) {
        pr_err("[%s] Failed to create sysfs trigger file\n", DRIVER_NAME);
        kobject_put(system_wq_kobj);
        return ret;
    }
 
    pr_info("[%s] Sysfs entry created: /sys/kernel/%s/trigger\n",
            DRIVER_NAME, DRIVER_NAME);
 
    /*
     * Schedule one work item from module_init to demonstrate that
     * schedule_work() can be called from any context.
     */
    pr_info("[%s] Scheduling initial work from module_init...\n",
            DRIVER_NAME);
 
    atomic_inc(&trigger_count);
    schedule_work(&my_work);
 
    pr_info("[%s] module_init done - work runs asynchronously\n",
            DRIVER_NAME);

    return 0;
}

static void __exit my_exit(void)
{
    pr_info("[%s] Unloading...\n", DRIVER_NAME);
 
    /*
     * Step 1: Remove sysfs trigger FIRST - no new work after this point.
     */
    sysfs_remove_file(system_wq_kobj, &trigger_attr.attr);
    kobject_put(system_wq_kobj);
 
    /*
     * Step 2: cancel_work_sync() - correct cleanup for schedule_work().
     *
     *   Work PENDING  → cancelled, will not run
     *   Work RUNNING  → waits for it to finish, then returns
     *
     * After this returns, the handler is guaranteed not to be running.
     *
     * NOTE: Do NOT use flush_scheduled_work() - it is deprecated.
     *       It flushes the ENTIRE system_wq, affecting all other
     *       kernel subsystems. cancel_work_sync() targets only our
     *       specific work item.
     */
    cancel_work_sync(&my_work);
 
    pr_info("[%s] Unloaded successfully | total triggers: %d\n",
            DRIVER_NAME, atomic_read(&trigger_count));
        
}

module_init(my_init);
module_exit(my_exit);


