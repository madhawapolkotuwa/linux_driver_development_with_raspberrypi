# Wait Queues

### Video :-
[![Youtube Video](https://img.youtube.com/vi/q2r0_LYCYvc/0.jpg)](https://www.youtube.com/watch?v=q2r0_LYCYvc)

A **wait queue** is a kernel-managed list of sleeping processes.    
When data is unavailable, a process is added to the queue and put to sleep - truly suspended, consuming zero CPU.   
When data arrives (from an IRQ handler, timer, or another process),     
the driver wakes up the processes in the queue and they resume execution.

This is the correct replacement for the busy-wait loop (`cpu_relax()`) shown in the previous section.

### Declaration:
```c++
#include <linux/wait.h>

/* Static declaration (preferred for module-level queues) */
static DECLARE_WAIT_QUEUE_HEAD(my_queue);

/* Dynamic declaration (use when allocated at runtime) */
wait_queue_head_t my_queue;
init_waitqueue_head(&my_queue);
```

### Sleeping: wait_event_interruptible()
```c++
/* Sleep until condition becomes true. Can be interrupted by signals. */
int ret = wait_event_interruptible(my_wait_queue, condition);
```

* Returns 0 when the condition becomes true.
* Returns `-ERESTARTSYS` if interrupted by a signal (e.g., `Ctrl+C`, `kill`). Your driver must propagate this to user space.

### Available Variants
Function | Signal-safe | Returns
---------|-------------|--------
`wait_event(q, cond)` | No | void
`wait_event_interruptible(q, cond)` | Yes | 0 or `-ERESTARTSYS`
`wait_event_timeout(q, cond, jiffies)` | No | remaining jiffies or 0
`wait_event_interruptible_timeout(q, cond, jiffies)` | Yes | remaining jiffies, 0, or `-ERESTARTSYS`

Always prefer `_interruptible` variants - processes stuck in uninterruptible sleep (`TASK_UNINTERRUPTIBLE`) cannot be killed with `Ctrl+C` or `kill -9`.

### Waking Up: `wake_up_interruptible()`
```c++
wake_up(&my_wait_queue);               /* Wake all sleepers (interruptible and uninterruptible) */
wake_up_interruptible(&my_wait_queue); /* Wake only TASK_INTERRUPTIBLE sleepers */
```
Call `wake_up_interruptible()` after producing new data - typically from an IRQ handler. Always pair it with `wait_event_interruptible()`.

## Example: Blocking and Non-Blocking I/O with `Wait Queue`. 
In this example, a GPIO button press acts as the hardware event that produces data. When the button is pressed, the ISR sets d`ata_available = 1` and wakes up any sleeping readers via the wait queue.

This replaces the busy-wait loop (`cpu_relax()`) from the previous section with proper kernel sleep.

**Key Changes from the Previous Driver**     

**ISR - wakes the wait queue on button press:**

```c++
static irqreturn_t isr(int irq, void *dev_id)
{
    pr_info("%s: GPIO interrupt occurred\n", my_device);

    data_available = 1;    /* Set condition BEFORE waking up */
    wake_up_interruptible(&my_wait_queue);       /* Wake sleeping readers */

    return IRQ_HANDLED;
}
```
`my_read()` - sleeps instead of busy-waiting:
```c++
static ssize_t my_read(struct file *file,
                       char __user *buf,
                       size_t len,
                       loff_t *off)
{
    pr_info("%s: read called\n", my_device);

    if (!data_available) {

        /* Non-blocking: return immediately */
        if (file->f_flags & O_NONBLOCK) {
            pr_info("%s: non-blocking mode - no data\n", my_device);
            return -EAGAIN;
        }

        /* Blocking: sleep until the button is pressed or a signal arrives */
        pr_info("%s: blocking mode - sleeping\n", my_device);

        int ret = wait_event_interruptible(my_wait_queue, data_available != 0);
        if (ret)
            return -ERESTARTSYS;  /* Interrupted by a signal - propagate to user space */
    }

    data_available = 0;

    if (copy_to_user(buf, message, sizeof(message)))
        return -EFAULT;

    return sizeof(message);
}
```

This driver:

* Sleeps in blocking mode when no data is available, consuming zero CPU.
* Returns `-EAGAIN` immediately in non-blocking mode.
* Wakes up sleeping readers when the GPIO button is pressed (falling-edge ISR).

------------------------------------

## Test Result

### Test 1: Non-Blocking Read

#### *Step 1* - Load the module
```bash
sudo insmod wait_queue.ko
```
Initially `data_available = 0`.

#### *Step 2* - Run the test app (no data available)
```bash
sudo ./test /dev/my_cdev0
```

Console output:
```
Read failed: Resource temporarily unavailable
```
Kernel log (`dmesg`):
```
my_cdev: device opened
my_cdev: read called
my_cdev: non-blocking mode - no data
my_cdev: device closed
```

Because the file was opened with `O_NONBLOCK` and `data_available == 0`,    
the driver immediately returns `-EAGAIN` without sleeping.

#### *Step 3* - Press the hardware button

The falling edge on GPIO 20 triggers the ISR:
```
my_cdev: GPIO Interrupt occoured
```
Now `data_available = 1`.

#### Step 4 - Run the test app again
```bash
sudo ./test /dev/my_cdev0
```

Console output:
```
Read success: Hello from Kernel!
```

Kernel log:
```
my_cdev: device opened
my_cdev: read called
my_cdev: device closed
```
Because `data_available == 1`,   
the driver skips the blocking check, copies the message to user space, resets `data_available = 0`, and returns.

same as the previos example.


### Test 2: Blocking Read with `cat`

#### *Step 1* : run test program
```bash
sudo cat /dev/my_cdev0
```
cat opens the device without `O_NONBLOCK` and calls `read()` in a loop:
```c++
while (read(fd, buf, size) > 0)
    write(stdout, buf, size);
```

`data_available == 0` → my_read calls `wait_event_interruptible()`.      
The process enters `TASK_INTERRUPTIBLE` state and is removed from the run queue. CPU usage: 0%.

#### *Step 2* : Press the hardware button

The ISR fires → `data_available = 1` → `wake_up_interruptible()`.    
The process wakes up, re-checks the condition (now `true`),     
copies the message to user space, resets `data_available = 0`,  
and returns. `cat` prints:
```
Hello from Kernel!
```
`cat` immediately calls `read()` again → process returns to sleep.  

Each button press produces exactly one line of output.

#### *Step 3* - Observe CPU usage

While `cat` is blocked and waiting for the next button press, open another terminal and run:
```
top
```
You will see the `cat` process consuming 0% CPU - it is truly sleeping.   

Compare this to the previous busy-wait implementation,  
 ```c++
 while (!data_available) 
            cpu_relax(); /* will take 100% cpu */
 ```
where `cat` consumed **100% CPU** while waiting.

#### *Step 4* - Exit with Ctrl+C

Because the process is in interruptible sleep, `Ctrl+C` works correctly.   
The signal interrupts `wait_event_interruptible()`,   
which returns `-ERESTARTSYS`. The driver propagates this to user space and the process exits cleanly.     
The module can then be unloaded normally.

-------------------



### Flow Diagrams
`my_read()` Execution Flow
```
my_read()
   │
   ▼
Check data_available
   │
   ├── If 1 ──────────────► copy_to_user() ──► return sizeof(message)
   │
   └── If 0
         │
         ▼
   Is O_NONBLOCK set?
         │
     ┌───┴───────────────────┐
     │                       │
    YES                      NO
     │                       │
     ▼                       ▼
 return -EAGAIN    wait_event_interruptible()
                             │
                             ▼
                 Process → TASK_INTERRUPTIBLE
                 Removed from run queue
                             │
                   (Scheduler runs others)
                             │
                       (Button Press)
                             │
                             ▼
                            ISR()
                   data_available = 1
                      wake_up_interruptible()
                             │
                             ▼
                   Process → TASK_RUNNING
                 Condition re-checked → TRUE
                             │
                             ▼
                       copy_to_user()
                             │
                             ▼
                   return sizeof(message)
```

Complete User Space / Kernel Space Flow
```
USER SPACE                          KERNEL SPACE
──────────────                  ──────────────────────────

cat /dev/my_cdev0
        │
        ▼
      read()
        │
        └──────────────────────────► my_read()
                                          │
                                          ▼
                                 wait_event_interruptible()
                                          │
                                 Process → TASK_INTERRUPTIBLE
                                 Removed from run queue
                                          │
                                (CPU free - runs other tasks)
                                          │
                                          ▼
                                    (Button press)
                                          │
                                        ISR()
                                 data_available = 1
                                 wake_up_interruptible()
                                          │
                                 Process → TASK_RUNNING
                                          │
                                          ▼
                                     copy_to_user()
                                          │
      read() returns ◄────────────────────┘
        │
        ▼
  "Hello from Kernel!"
        │
        ▼
  (cat loops - calls read() again → process sleeps again)

```
---------------------------------

## That's Why Wait Queues Are Better Than Busy-Waiting

|           | Busy-wait (`cpu_relax()`) | Wait queue |
|-----------|---------------------------|------------|
CPU usage while waiting | 100% of one core | 0% |
Power consumption | High | Minimal
Interruptible by signals | No | Yes (with `_interruptible`)
Process can be killed | Not while spinning | Yes
Module can be unloaded | Not while a process is spinning | Yes
Suitable for production | ❌ Never | ✅ Yes

## Summary

API | Description
----|------------
`DECLARE_WAIT_QUEUE_HEAD(q)` | Statically declares and initializes a wait queue
`init_waitqueue_head(&q)` | Dynamically initializes a wait queue
`wait_event_interruptible(q, cond)` | Sleeps until `cond` is true; wakes on signals
`wake_up_interruptible(&q)` | Wakes all TASK_INTERRUPTIBLE sleepers in the queue
`TASK_INTERRUPTIBLE` | Process state while sleeping in the wait queue
`TASK_RUNNING` | Process state after being woken up by `wake_up_interruptible()`
`-ERESTARTSYS` | Returned by `wait_event_interruptible()` when interrupted by a signal


**Key rule**: Always set the condition variable before calling `wake_up_interruptible()`.   
If you wake first and set the condition after, the woken process may re-check the condition while it is still false,    
go back to sleep, and never be woken again - a classic kernel deadlock.
