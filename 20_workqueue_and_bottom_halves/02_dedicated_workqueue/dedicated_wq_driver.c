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

#define DRIVER_NAME  "dedicated_wq"

/* ------------------------------------------------------------------ */
/*  Work item definition                                                */
/* ------------------------------------------------------------------ */
struct demo_work_data {
    struct work_struct work;   /* must be first or use container_of */
    unsigned int      job_id;
};


/* We embed a counter so the handler knows which invocation it is */
static atomic_t job_counter = ATOMIC_INIT(0);

static struct workqueue_struct *demo_wq;

/**
 * demo_work_handler - Bottom-half work handler
 *
*/
static void demo_work_handler(struct work_struct *work)
{
    struct demo_work_data *data =
        container_of(work, struct demo_work_data, work);

    pr_info("[%s] Work handler running | job_id=%u | PID=%d | CPU=%d\n",
            DRIVER_NAME, data->job_id, current->pid, smp_processor_id());

    /* Simulate some "heavy" deferred work, safe because we are in
     * process context, NOT in interrupt context.                      */
    pr_info("[%s] Doing deferred work (sleeping 1000ms is OK here)...\n",
            DRIVER_NAME);
    msleep(2000);

    pr_info("[%s] Deferred work done for job_id=%u\n",
            DRIVER_NAME, data->job_id);

    /* Free the dynamically allocated work data */
    kfree(data);
}

/* ------------------------------------------------------------------ */
/*  Helper: allocate + schedule one work item on dedicated_wq             */
/* ------------------------------------------------------------------ */

static int schedule_demo_work(void)
{
    struct demo_work_data *data;

    data = kmalloc(sizeof(*data), GFP_KERNEL);
    if (!data) {
        pr_err("[%s] Failed to allocate work data\n", DRIVER_NAME);
        return -ENOMEM;
    }

    data->job_id = atomic_inc_return(&job_counter);

    /* INIT_WORK binds the work_struct to its handler function */
    INIT_WORK(&data->work, demo_work_handler);

    pr_info("[%s] Queueing job_id=%u on demo_wq\n",
            DRIVER_NAME, data->job_id);

    /*
     * queue_work() places the job on OUR private workqueue.
     *
     * Previously this used schedule_work() which queues on system_wq 
     * that is still fine, but then flush_scheduled_work() would be
     * needed on exit, which is deprecated.
     *
     * Using queue_work(demo_wq, ...) means we can flush only our own
     * queue with flush_workqueue(demo_wq) clean and scoped.
     */
    queue_work(demo_wq, &data->work);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Sysfs interface: echo 1 | sudo tee /sys/kernel/dedicated_wq/trigger       */
/* ------------------------------------------------------------------ */

static struct kobject *demo_kobj;

static ssize_t trigger_store(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             const char *buf, size_t count)
{
    int val;

    if (kstrtoint(buf, 10, &val) != 0)
        return -EINVAL;

    if (val == 1) {
        pr_info("[%s] Sysfs trigger received - scheduling work\n",
                DRIVER_NAME);
        schedule_demo_work();
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

    pr_info("[%s] Loading \n", DRIVER_NAME);

    /* Create the module-private workqueue first all other setup
     * depends on it being available before work is scheduled.        */
    demo_wq = alloc_workqueue(DRIVER_NAME, WQ_UNBOUND, 0);
    if (!demo_wq) {
        pr_err("[%s] Failed to create workqueue\n", DRIVER_NAME);
        return -ENOMEM;
    }
    pr_info("[%s] Private workqueue '%s' created\n", DRIVER_NAME, DRIVER_NAME);

    /* Create /sys/kernel/dedicated_wq/ */
    demo_kobj = kobject_create_and_add(DRIVER_NAME, kernel_kobj);
    if (!demo_kobj) {
        pr_err("[%s] Failed to create kobject\n", DRIVER_NAME);
        destroy_workqueue(demo_wq);
        return -ENOMEM;
    }

    ret = sysfs_create_file(demo_kobj, &trigger_attr.attr);
    if (ret) {
        pr_err("[%s] Failed to create sysfs file\n", DRIVER_NAME);
        kobject_put(demo_kobj);
        destroy_workqueue(demo_wq);
        return ret;
    }

    pr_info("[%s] Sysfs entry: /sys/kernel/%s/trigger\n",
            DRIVER_NAME, DRIVER_NAME);

    /* Schedule an initial work item from init to demonstrate the concept */
    pr_info("[%s] Scheduling initial work from module_init...\n",
            DRIVER_NAME);
    schedule_demo_work();

    pr_info("[%s] module_init returns - work will execute asynchronously\n",
            DRIVER_NAME);

    return 0;
}

static void __exit my_exit(void)
{
    pr_info("[%s] Unloading.. flushing and destroying demo_wq...\n",
            DRIVER_NAME);

    /*
     * Remove sysfs trigger FIRST so no new work can be queued
     * after we start the flush.
     */
    sysfs_remove_file(demo_kobj, &trigger_attr.attr);
    kobject_put(demo_kobj);

    flush_workqueue(demo_wq);
    destroy_workqueue(demo_wq);

    pr_info("[%s] Unloaded successfully\n", DRIVER_NAME);
        
}

module_init(my_init);
module_exit(my_exit);


