#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/atomic.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Dedicated Workqueue Example");


#define DRIVER_NAME   "dedicated_wq_demo"
#define DEVICE_NAME   "dedicated_wq_dev"

/* ------------------------------------------------------------------ */
/*  Work item - carries a user-supplied message + job ID               */
/* ------------------------------------------------------------------ */

struct wq_job {
    struct work_struct  work;
    unsigned int        job_id;
    char                message[WRITE_BUF_MAX];
};


/* Our dedicated workqueue - lives for the lifetime of the module */
static struct workqueue_struct *my_wq;

static atomic_t job_counter = ATOMIC_INIT(0);

static void wq_job_handler(struct work_struct *work)
{
    struct wq_job *job = container_of(work, struct wq_job, work);

    pr_info("[%s] Job #%u executing | msg='%s' | PID=%d | CPU=%d\n",
            DRIVER_NAME, job->job_id, job->message,
            current->pid, smp_processor_id());

    /* Demonstrate: process context allows sleeping */
    msleep(2000);

    pr_info("[%s] Job #%u done\n", DRIVER_NAME, job->job_id);
    kfree(job);
}

/* ------------------------------------------------------------------ */
/*  Character device                                                    */
/* ------------------------------------------------------------------ */

static dev_t   dev_num;
static struct cdev     my_cdev;
static struct class   *my_class;

/**
 * device_write - called when user writes to /dev/dedicated_wq_dev
 *
 * Allocates a wq_job and queues it on my_wq (our dedicated workqueue).
 * Returns immediately - the actual work is done asynchronously.
 */
static ssize_t device_write(struct file *filp, const char __user *buf,
                            size_t len, loff_t *off)
{
    struct wq_job *job;
    size_t copy_len = min(len, (size_t)(WRITE_BUF_MAX - 1));

    job = kmalloc(sizeof(*job), GFP_KERNEL);
    if (!job) {
        pr_err("[%s] Failed to allocate job\n", DRIVER_NAME);
        return -ENOMEM;
    }

    if (copy_from_user(job->message, buf, copy_len)) {
        kfree(job);
        return -EFAULT;
    }
    job->message[copy_len] = '\0';

    /* Strip trailing newline from echo */
    if (copy_len > 0 && job->message[copy_len - 1] == '\n')
        job->message[copy_len - 1] = '\0';

    job->job_id = atomic_inc_return(&job_counter);

    INIT_WORK(&job->work, wq_job_handler);

    pr_info("[%s] Queueing job #%u on dedicated workqueue\n",
            DRIVER_NAME, job->job_id);

    /* queue_work() places the work on OUR workqueue, not system_wq.
     * This gives us isolation from other kernel subsystems.           */
    queue_work(my_wq, &job->work);

    return len;  /* report all bytes consumed */
}

static int device_open(struct inode *inode, struct file *filp)
{
    pr_info("[%s] Device opened\n", DRIVER_NAME);
    return 0;
}

static int device_release(struct inode *inode, struct file *filp)
{
    pr_info("[%s] Device released\n", DRIVER_NAME);
    return 0;
}

static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = device_open,
    .release = device_release,
    .write   = device_write,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static int __init my_init(void)
{

    int ret;

    pr_info("[%s] Loading\n", DRIVER_NAME);

    /* 1. Create our dedicated workqueue
     *    WQ_UNBOUND  : not pinned to a specific CPU
     *    max_active=4: allow up to 4 jobs to execute concurrently      */
    my_wq = alloc_workqueue("%s", WQ_UNBOUND, 4, DRIVER_NAME);
    if (!my_wq) {
        pr_err("[%s] Failed to create workqueue\n", DRIVER_NAME);
        return -ENOMEM;
    }
    pr_info("[%s] Dedicated workqueue created (WQ_UNBOUND, max_active=4)\n",
            DRIVER_NAME);

    /* 2. Allocate char device region */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("[%s] alloc_chrdev_region failed: %d\n", DRIVER_NAME, ret);
        goto err_wq;
    }

    /* 3. Init and add cdev */
    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("[%s] cdev_add failed: %d\n", DRIVER_NAME, ret);
        goto err_chrdev;
    }

    /* 4. Create device class and device node */
    my_class = class_create(DEVICE_NAME);
    if (IS_ERR(my_class)) {
        ret = PTR_ERR(my_class);
        pr_err("[%s] class_create failed: %d\n", DRIVER_NAME, ret);
        goto err_cdev;
    }

    if (IS_ERR(device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME))) {
        pr_err("[%s] device_create failed\n", DRIVER_NAME);
        ret = -EFAULT;
        goto err_class;
    }

    pr_info("[%s] Device /dev/%s created. Major=%d Minor=%d\n",
            DRIVER_NAME, DEVICE_NAME,
            MAJOR(dev_num), MINOR(dev_num));
    pr_info("[%s] Try: echo 'hello' > /dev/%s\n", DRIVER_NAME, DEVICE_NAME);

    return 0;

err_class:
    class_destroy(my_class);
err_cdev:
    cdev_del(&my_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, 1);
err_wq:
    destroy_workqueue(my_wq);
    return ret;
}

static void __exit my_exit(void)
{
    pr_info("[%s] Unloading - flushing and destroying workqueue...\n",
            DRIVER_NAME);

    /* flush_workqueue() blocks until all queued jobs finish.
     * ALWAYS call this before destroy_workqueue() to prevent
     * accessing freed memory from a still-running work handler. */
    flush_workqueue(my_wq);
    destroy_workqueue(my_wq);

    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("[%s] Unloaded successfully\n", DRIVER_NAME);
}

module_init(my_init);
module_exit(my_exit);

