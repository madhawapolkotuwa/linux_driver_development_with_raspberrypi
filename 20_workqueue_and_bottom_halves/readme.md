# Workqueues & Bottom Halves

## Video :-

[![Youtube Video](https://img.youtube.com/vi/tRA5ZTiCCU8/0.jpg)](https://www.youtube.com/watch?v=tRA5ZTiCCU8)



When a hardware interrupt fires, the kernel runs the ISR (Interrupt Service Routine) immediately ➡ in hardirq context.     
This context has strict rules: **no sleeping**, **no blocking**, **no heavy work**. `It must return as fast as possible.`  

But real drivers often need to do slow work in response to an interrupt :   
 Ex:- read an I2C sensor, write to a file, wait for a bus transaction. 
 
This is the problem **bottom halves solve**.

We talk this in [12_gpio_irq/top_bottom_half](../12_gpio_irq/top_bottom_half/) example.


```
Hardware IRQ fires
       │
       ▼
  ┌─────────────┐
  │  TOP HALF   │  ← ISR runs here (hardirq context)
  │    (ISR)    │    Fast! ACK interrupt, queue work, return.
  └──────┬──────┘
         │  schedule_work()
         ▼
  ┌─────────────┐
  │ BOTTOM HALF │  ← Work handler runs here (process context)
  │  (Workqueue)│    Can sleep. Can use mutex. Can do I2C.
  └─────────────┘
```

A **workqueue** is the modern, recommended mechanism to implement bottom halves in Linux kernel drivers.

-------------------

## 1. Deferred Execution

### 1.1 What is Deferred Execution?

Deferred execution means: `do this work later`, `outside of interrupt context.`

The kernel provides a pool of worker threads called **kworker** threads. When we schedule a work item, a kworker thread picks it up and runs our handler function in process context, the same context as any normal kernel thread.

we can verify this by running on linux system:
```bash
ps aux | grep kworker
```

we will see several `kworker` threads already running. These are the threads that execute our work handlers.

### 1.2 The Two Halves on Raspberry Pi

Consider a GPIO button connected to **GPIO 20** and an LED on **GPIO 21**.

Without deferred execution ➡ everything in the ISR:
```c++
static irqreturn_t button_isr(int irq, void *dev_id)
{
    // ❌ This is WRONG - msleep() in ISR will crash the kernel
    msleep(500);
    gpio_set_value(GPIO_LED, 1);
    return IRQ_HANDLED;
}
```

With deferred execution ➡ split into top half and bottom half:

```c++
/* BOTTOM HALF - runs in process context, sleep is safe */
static void led_work_handler(struct work_struct *work)
{
    msleep(500);                  // ✅ Safe - process context
    gpio_set_value(GPIO_LED, 1);  // Turn LED on after 500ms delay
    msleep(1000);
    gpio_set_value(GPIO_LED, 0);  // Turn LED off
}

static DECLARE_WORK(led_work, led_work_handler);

/* TOP HALF - ISR, runs in hardirq context */
static irqreturn_t button_isr(int irq, void *dev_id)
{
    schedule_work(&led_work);  // ✅ Queue the work and return fast
    return IRQ_HANDLED;
}
```

📍 Real hardware demo: Press the button → ISR fires instantly → LED turns ON after a 500ms delay. The delay happens in the work handler, not the ISR. 　     
Check the Exampel :- [12_gpio_irq/top_bottom_half](../12_gpio_irq/top_bottom_half/)


### 1.3 What Process Context Allows

This table shows why getting out of interrupt context matters:

Operation | ISR (hardirq) | Work Handler (process ctx) |
----------|---------------|----------------------------|
msleep() / ssleep() | ❌ Will BUG | ✅ Safe |
mutex_lock() | ❌ May deadlock | ✅ Safe |
kmalloc(GFP_KERNEL) | ❌ May sleep | ✅ Safe |
i2c_transfer() | ❌ Sleeps internally | ✅ Safe |
gpio_set_value() | ✅ Non-sleeping | ✅ Safe |
atomic operations | ✅ Always safe | ✅ Safe |

### 1.4 Core API : Declaring and Scheduling Work

```c++
#include <linux/workqueue.h>

/* --- Static declaration --- */
static DECLARE_WORK(my_work, my_work_handler);

/* --- Dynamic declaration (for work inside a struct) --- */
struct my_data {
    struct work_struct work;
    int value;
};
INIT_WORK(&data->work, my_work_handler);

/* --- Schedule on system_wq (runs as soon as a kworker is free) --- */
schedule_work(&my_work);

/* --- Recover -> struct inside the handler --- */
static void my_work_handler(struct work_struct *work)
{
    struct my_data *data = container_of(work, struct my_data, work);
    // use data->value
}
```

### 1.5 Cleanup on Module Exit

Always cancel or flush pending work before `rmmod`.      
Without this, a work handler may run after the module's memory is freed ➡ causing a kernel oops.

```c++
static void __exit my_driver_exit(void)
{
    /* Waits if the handler is currently running, then cancels */
    cancel_work_sync(&data->work);
}
```

>> ⚠️ Never use `flush_scheduled_work()` ➡ it is deprecated since it flushes the entire system_wq, affecting unrelated kernel subsystems.

### 1.6 Example

📄 [01_system_workqueue/system_wq_driver.c](01_system_workqueue/system_wq_driver.c)

```bash
sudo insmod system_wq_driver.ko
echo 1 | sudo tee /sys/kernel/system_wq_demo/trigger
sudo rmmod system_wq_driver
dmesg | tail -20
```

## 2. System vs Dedicated Workqueues

### 2.1 The Shared System Workqueue

`system_wq` is a kernel-global workqueue shared by all subsystems. `schedule_work()` queues work on it.     
It is convenient but has trade-offs:
* Pro: No setup required, always available.
* Con: Work from many subsystems competes for the same **kworker** threads.
* Con: A slow work item can delay unrelated work across the kernel.

### 2.2 Creating a Dedicated Workqueue
A dedicated workqueue gives to the driver its own pool of worker threads, isolated from the rest of the kernel.

```c++
struct workqueue_struct *my_wq;

/* Create in module init */
my_wq = alloc_workqueue("my_driver_wq", WQ_UNBOUND, 0);
if (!my_wq)
    return -ENOMEM;

/* Queue work on the workqueue */
queue_work(my_wq, &my_work);

/* Destroy in module exit */
flush_workqueue(my_wq);     // wait for all pending jobs
destroy_workqueue(my_wq);   // free the queue
```

After `alloc_workqueue()` we will see the new kworker thread on the Pi:
```bash
ps aux | grep my_driver_wq
```

### 2.3 alloc_workqueue Flags

Flag | Meaning | 
-----|---------|
`WQ_UNBOUND` | Not pinned to a specific CPU. Good for general driver work. | 
`WQ_HIGHPRI` | Higher priority kworker threads. For latency-sensitive work. | 
`WQ_MEM_RECLAIM` | Guarantees a thread is always available even under memory pressure. Use in storage/network drivers. |
`0` | Default : bound to CPU, normal priority. |

The `max_active` parameter controls how many work items may execute concurrently. `0` means use the system default.

```c++
/* Examples */
alloc_workqueue("my_wq", WQ_UNBOUND, 0);       // unbound, default concurrency
alloc_workqueue("my_wq", WQ_HIGHPRI, 1);       // high priority, 1 at a time
alloc_workqueue("my_wq", WQ_UNBOUND, 4);       // up to 4 concurrent jobs
```

### 2.4 Dedicated Workqueue example

📄 [02_dedicated_workqueue/dedicated_wq_driver.c](02_dedicated_workqueue/dedicated_wq_driver.c)

### 2.5 System vs Dedicated : When to Use Which
Situation | Use | 
----------|-----|
Simple, occasional background work | `schedule_work()` on `system_wq` | 
Work triggered from GPIO ISR | `schedule_work()` or `queue_work()`|
High-frequency or latency-sensitive work | Dedicated `WQ_HIGHPRI` | 
Driver needs clean, isolated flush on exit | Dedicated workqueue | 
Multiple work items that can run in parallel | Dedicated with `max_active` > 1 |

### 2.6 Delayed Work
`delayed_work` runs a work handler after a configurable time delay. It uses `system_wq` internally unless we specify our own queue.

Compare with [13_hrtimer](../13_hrtimer):    
|  | hrtimer | delayed_work |
|--|--------|--------------|
Callback context | Hardirq / softirq | Process context |
Can sleep in callback | ❌ No | ✅ Yes |
Typical use | Precise timing, short actions | Deferred driver work with delay |

```c++
static struct delayed_work my_delayed_work;

/* Init */
INIT_DELAYED_WORK(&my_delayed_work, my_delayed_handler);

/* Schedule - runs after 2 seconds */
queue_delayed_work(system_wq, &my_delayed_work, msecs_to_jiffies(2000));

/* Self-reschedule inside handler (periodic pattern) */
static void my_delayed_handler(struct work_struct *work)
{
    struct delayed_work *dw = to_delayed_work(work);

    pr_info("Tick - jiffies=%lu\n", jiffies);

    /* Reschedule for next period */
    queue_delayed_work(system_wq, dw, msecs_to_jiffies(2000));
}

/* Cancel on exit - _sync waits for running handler */
cancel_delayed_work_sync(&my_delayed_work);
```
>> 📍 Real hardware demo: An LED blinks periodically using self-rescheduling `delayed_work`.     


### 2.7 Examples

📄 [03_delayed_work/delayed_work_driver.c](03_delayed_work/delayed_work_driver.c)
```bash
sudo insmod delayed_work_driver.ko period_ms=2000
dmesg -w                       # watch periodic ticks
sudo rmmod delayed_work_driver
```

## 3. Comparison with Tasklets

### 3.1 A Brief History

Before workqueues became the standard, the kernel had two softirq-based bottom-half mechanisms:

* **softirq** : static, compiled into the kernel, used only by core subsystems (networking, block layer). Not for drivers.
* **tasklet** : a dynamic wrapper around softirq, available to drivers.

Tasklets were widely used in older drivers. However, they run in softirq context,    
which is still atomic, still cannot sleep. They were deprecated in kernel 5.x and are scheduled for removal.

### 3.2 Execution Context : The Critical Difference

```
Hardware IRQ
     │
     ▼
 Top Half (ISR)          ← hardirq context   - atomic
     │
     |
 Bottom Half
     ├──► Tasklet        ← softirq context   - atomic  ❌ cannot sleep
     │
     └──► Workqueue      ← process context   - sleepable ✅ can sleep
```

Both tasklets and workqueues are bottom halves :     
 they both run after the ISR returns. The difference is **which context they run in**.

### 3.3 Side-by-Side API Comparison

```c++
/* ── TASKLET (deprecated) ─────────────────────────────── */

static void tasklet_handler(struct tasklet_struct *t)
{
    // Softirq context - atomic
    // ❌ msleep() here → BUG: scheduling while atomic
    gpio_set_value(GPIO_LED, 1);   // OK - non-sleeping
}

static DECLARE_TASKLET(my_tasklet, tasklet_handler);

// Schedule from ISR:
tasklet_schedule(&my_tasklet);

// Cleanup:
tasklet_kill(&my_tasklet);


/* ── WORKQUEUE (recommended) ──────────────────────────── */

static void wq_handler(struct work_struct *work)
{
    // Process context - sleepable
    msleep(500);                   // ✅ Safe
    gpio_set_value(GPIO_LED, 1);   // ✅ Safe
}

static DECLARE_WORK(my_work, wq_handler);

// Schedule from ISR:
schedule_work(&my_work);

// Cleanup:
cancel_work_sync(&my_work);
```


### 3.4 Feature Comparison Table

Feature | Tasklet | Workqueue | 
--------|---------|-----------|
Execution context | Softirq (atomic) | Process context | 
msleep() / ssleep() | ❌ Will BUG | ✅ Safe | 
mutex_lock() | ❌ Will deadlock | ✅ Safe | 
I2C / SPI transactions | ❌ Not possible | ✅ Safe |
gpio_set_value() | ✅ OK | ✅ OK | 
Per-CPU execution | Yes | Configurable | 
Priority control | ❌ None | ✅ WQ_HIGHPRI |
Kernel status | ⚠️ Deprecated (5.x) | ✅ Recommended |


### 3.5 The BUG You Will See if You Try to Sleep in a Tasklet
In `tasklet_vs_workqueue` there is a commented line inside the tasklet handler:

```c++
static void tasklet_led_handler(struct tasklet_struct *t)
{
    /* msleep(500); */   // ← If we use this here kernel will crash.

    gpio_set_value(GPIO_LED, 1);
}
```

Uncomment it, rebuild, load the module, and press the button. 
Kernel crash and (we need to restart the system)

### 3.6 The `in_interrupt()` Probe

The `05_tasklet_vs_workqueue` driver prints `in_interrupt()` from both handlers. Load it in each mode and compare the output:
```bash
# Tasklet mode
sudo insmod tasklet_vs_wq_driver.ko mode=1
# Press button - dmesg shows:
# [tasklet_vs_wq] TASKLET (softirq ctx) | in_interrupt()=2   ← non-zero!

# Workqueue mode
sudo insmod tasklet_vs_wq_driver.ko mode=2
# Press button - dmesg shows:
# [tasklet_vs_wq] WORKQUEUE (process ctx) | in_interrupt()=0  ← zero = safe to sleep
```
`in_interrupt()` returning non-zero means you are in atomic context ➡ sleeping here will crash the kernel.   
Returning `0` confirms process context ➡ sleeping is safe.

### 3.7 Why Tasklets Were Deprecated

The kernel community deprecated tasklets for three reasons:

1. `Softirq starvation` ➡ a misbehaving tasklet could delay networking and other softirq work on the same CPU.
2. `No sleep = workarounds` ➡ drivers that needed a delay had to schedule another bottom half (a timer or workqueue), adding complexity for no benefit.     
3. `Workqueues do everything tasklets do, plus more` ➡ there is no scenario where a tasklet is the better choice in new code.

>> **⚠️ Rule for new drivers**: `Never use tasklets. Use workqueues`. 

### 3.8 Example

📄 [04_tasklet_vs_workqueue/tasklet_vs_wq_driver.c](04_tasklet_vs_workqueue/tasklet_vs_wq_driver.c)
```bash
# Load in tasklet mode - LED turns ON instantly, no delay possible
sudo insmod tasklet_vs_wq_driver.ko mode=1
# Press button, observe dmesg

sudo rmmod tasklet_vs_wq_driver

# Load in workqueue mode - LED turns ON after 500ms delay
sudo insmod tasklet_vs_wq_driver.ko mode=2
# Press button, observe dmesg

sudo rmmod tasklet_vs_wq_driver
```

## Examples Summary

|  | Example | Subtopic | What it shows |
|--|--------|----------|--------------|
01 | system_wq_driver | Deferred Execution | INIT_WORK, queue_work, sysfs trigger | 
02 | dedicated_wq_driver | System vs Dedicated | alloc_workqueue, char device, isolated flush |
03 | delayed_work_driver | System vs Dedicated | `INIT_DELAYED_WORK`, self-rescheduling, period_ms param | 
04 | `tasklet_vs_wq_driver` | Comparison with Tasklets | Same hardware, mode=1 tasklet vs mode=2 workqueue

```c++
/* --- Work declaration --- */
DECLARE_WORK(name, handler);
INIT_WORK(&work, handler);
INIT_DELAYED_WORK(&dwork, handler);

/* --- Scheduling --- */
schedule_work(&work);                              // → system_wq
queue_work(wq, &work);                            // → your wq
queue_delayed_work(wq, &dwork, msecs_to_jiffies(ms));

/* --- Cleanup (always call on module exit) --- */
cancel_work_sync(&work);
cancel_delayed_work_sync(&dwork);
flush_workqueue(wq);
destroy_workqueue(wq);

/* --- Dedicated workqueue lifecycle --- */
wq = alloc_workqueue("name", flags, max_active);
flush_workqueue(wq);
destroy_workqueue(wq);
```
---------------



