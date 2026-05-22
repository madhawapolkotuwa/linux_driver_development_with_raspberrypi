# Mutex

## Video :-

[![Youtube Video](https://img.youtube.com/vi/LYRlDIB_Tlw/0.jpg)](https://www.youtube.com/watch?v=LYRlDIB_Tlw)

The previous examples showed how a process waits for data from a device using 
* [blocking I/O](/18_wait_queue_poll_fasync/01_busy_wait/)
* [wait queue](/18_wait_queue_poll_fasync/02_wait_queue/)
* [poll/select](/18_wait_queue_poll_fasync/03_poll_select/)
* [asynchronous notification with `fasync`](/18_wait_queue_poll_fasync/04_fasync/).

All of those mechanisms are about **waiting for an event**.

A `mutex` is used to **protect shared data**, ensuring that it is not accessed by multiple callers at the same time.

Example :-   
Imagine two processes opening the same device file and both calling `write()` at the same time.
Since modern systems have multiple CPU cores, both processes can enter the driver simultaneously and execute in parallel.    

Now, if both are modifying the same shared buffer without any protection, this can lead to **data corruption** or **inconsistent state**.

This is where a `mutex` ➞ short for mutual exclusion ➞ comes into play.

A `mutex` ensures that **only one caller can access the critical section at a time**.    
If another caller tries to acquire the mutex while it’s already held, it will **sleep until the mutex is released**.

```c
    Process A                      Process B  
─────────────────              ─────────────────

write() → my_write()           write() → my_write()
        │                              │
        ▼                              ▼
mutex_lock(&my_mutex)          mutex_lock(&my_mutex)
  ✅ Acquired                    ❌ Already held → sleeps
        │
        ▼
  /* modifies shared buffer */
        │
        ▼
mutex_unlock(&my_mutex)    ← wakes up, acquires mutex
                                       │
                                       ▼
                           /* modifies shared buffer */
                                       │
                                       ▼
                               mutex_unlock(&my_mutex)
```

Only one process touches the shared buffer at a time.    
The other is **sleeping**, it does not burn CPU while waiting.

--------------

### How It Works
A mutex has two states: locked and unlocked.

`mutex_lock()` ➞ acquire the mutex. If already held, the caller sleeps until it is released.    
`mutex_unlock()` ➞ release the mutex. Wakes up the next waiter if any.         
`mutex_trylock()` ➞ try to acquire without sleeping. Returns 1 on success, 0 if already held.

* The kernel enforces one strict rule:     
**the same task that locked the mutex must be the one to unlock it.**   
This is what distinguishes a `mutex` from a `semaphore`.

---------------

### Driver-Side Implementation

#### 1. Declare and Initialize the Mutex

```c++
#include <linux/mutex.h>

static struct mutex my_mutex;

static int __init my_init(void)
{
    mutex_init(&my_mutex);
    /* ... rest of init ... */
}
```

Or statically with the macro:
```c++
static DEFINE_MUTEX(my_mutex);
```

#### 2. Protect the Critical Section

ex:- `.write`
```c++
static ssize_t my_write(struct file *file, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    /* Acquire the mutex - sleeps if another process holds it.
       Returns -EINTR if interrupted by a signal while sleeping. */
    if (mutex_lock_interruptible(&my_mutex))
        return -EINTR;

    /* --- Critical section --- */
    memset(shared_buffer, 0, BUFFER_SIZE);
    if (copy_from_user(shared_buffer, buf, count)) {
        mutex_unlock(&my_mutex);
        return -EFAULT;
    }
    shared_len = count;
    /* --- End of critical section --- */

    mutex_unlock(&my_mutex);
    return count;
}
```
Use `mutex_lock_interruptible()` instead of `mutex_lock()` in file operations.   
If the process receives a signal while sleeping on the mutex,    
`mutex_lock_interruptible()` returns `-EINTR` so the signal is not silently swallowed.

ex :- `.read`
```c++
static ssize_t my_read(struct file *file, char __user *buf,
                       size_t count, loff_t *ppos)
{
    ssize_t ret;

    if (mutex_lock_interruptible(&my_mutex))
        return -EINTR;

    /* --- Critical section --- */
    ret = copy_to_user(buf, shared_buffer, shared_len) ? -EFAULT : shared_len;
    /* --- End of critical section --- */

    mutex_unlock(&my_mutex);
    return ret;
}
```

#### 3. Destroy the Mutex on Exit
```c++
static void __exit my_exit(void)
{
    mutex_destroy(&my_mutex);
    /* ... rest of cleanup ... */
}
```
`mutex_destroy()` is a debug-mode check that catches code that destroys a mutex
while it is still held.         
It is a no-op in release builds but important to include.

--------------------------
--------------------------
<br>

### Important Notes
* **Mutexes cannot be used in interrupt context**
`mutex_lock()` may sleep.          
Sleeping is not allowed in an `ISR` or any atomic context.

* If we need to protect data shared between an `ISR` and a process context, use a [`spinlock`](/19_kernel_synchronization/02_spinlock/) instead.

* Always unlock on every exit path
If the critical section has multiple return statements, each one must call `mutex_unlock()` first.

* ⚠️ A missed unlock means the mutex is held forever, every subsequent caller sleeps indefinitely. 

```c++
/* Wrong - leaks the mutex on error */
if (mutex_lock_interruptible(&my_mutex))
    return -EINTR;

if (copy_from_user(...))
    return -EFAULT;       /* ← mutex never unlocked! */

mutex_unlock(&my_mutex);
```

```c++
/* Correct - unlock on every exit path */
if (mutex_lock_interruptible(&my_mutex))
    return -EINTR;

if (copy_from_user(...)) {
    mutex_unlock(&my_mutex);   /* ← always unlock before returning */
    return -EFAULT;
}

mutex_unlock(&my_mutex);
```

* **Do not re-acquire a mutex that already hold**
Linux mutexes are not recursive.        
If the same task calls `mutex_lock()` twice on the same mutex without unlocking in between,      
it deadlocks immediately ➞ it sleeps waiting for itself.
```c++
mutex_lock(&my_mutex);
    /* ... */
    mutex_lock(&my_mutex);   /* ← DEADLOCK - task sleeps waiting for itself */
```

* `mutex_trylock()` for non-blocking paths
```c++
if (!mutex_trylock(&my_mutex))
    return -EBUSY;   /* Someone else holds it - return immediately instead of sleeping */

/********************/
/* critical section */
/********************/

mutex_unlock(&my_mutex);
```
Useful when the caller cannot afford to sleep, or to implement a "**best effort**" fast path.


### Mutex lock methods

|        Call                |          Behaviour when mutex is held   
|----------------------------|-----------------------------------------
|`mutex_lock()`              | Sleeps unconditionally ➞ signal cannot wake it
|`mutex_lock_interruptible()`| Sleeps but wakes on signal ➞ returns `-EINTR`
|`mutex_lock_killable()`     | Wakes only on fatal signals (`SIGKILL`)
|`mutex_trylock()`           | Never sleeps ➞ returns immediately

--------------------------------------

Always prefer `mutex_lock_interruptible()` in file operations
so that `Ctrl+C` from user space can unblock a waiting process.


------------------------------------------------------------------
------------------------------------------------------------------
------------------------------------------------------------------

<br>
<br>

## Linux : `thread priority`

The priority of Linux kernel threads (`kthreads`).    
The kernel handles everything from critical hardware interrupts to background disk cleanup, managing priorities is essential to keep the system stable and responsive.

In Linux, thread priority is generally tied to the **scheduling policy** assigned to that thread.

### 1. Scheduling Policies and Priority Ranges
The Linux kernel uses different schedulers depending on how "**urgent**" a task is. Priorities are handled differently in each:

* **Completely Fair Scheduler (CFS):**  
This is for standard tasks. Instead of a "priority" in the traditional sense, these use `Nice values` ranging from `-20 (highest priority)` to `19 (lowest priority)`.

* **Real-Time (RT) Schedulers:**    
These are for time-sensitive tasks (`SCHED_FIFO` or `SCHED_RR`).    
They use a priority scale from 1 to 99.     
In this world, 99 is the highest priority, meaning it will preempt almost anything else on the system.

### 2. How to Set Priority Programmatically
If we are writing a kernel module, we typically set the priority immediately after creating the thread or within the thread function itself.

**Using `sched_setscheduler`**  
This is the most common way to move a kthread into a Real-Time priority bracket.
```c++
struct task_struct *task;
struct sched_param param = { .sched_priority = 95 }; // High priority

{ // init after create task
// Set policy to SCHED_FIFO with priority 95
sched_setscheduler(task, SCHED_FIFO, &param);
}
```
**Using `set_user_nice`**   
If the thread isn't "mission critical" and should just run in the background without lagging the UI, we can adjust its nice value:
```c++
set_user_nice(current, 10); // Shifts it toward lower priority
```
### 3. Visualizing Priority Levels
The kernel sees priorities on a unified internal scale (0–139), where lower numbers actually represent higher urgency.

| Category | Policy | User-Level Priority / Nice | Internal Kernel Priority
-----------|--------|----------------------------|-----------------
Real-Time|`SCHED_FIFO` / `RR`|99 (Highest) down to 1|0 to 98
Normal|`SCHED_NORMAL`|-20 (Nice) to +19 (Nice)|100 to 139

### Important Considerations
* **Starvation:** If we set a kthread to `SCHED_FIFO` at priority 99 and it enters an infinite loop, the system will likely lock up because the kernel will prioritize that thread over almost everything else
including the mouse and keyboard inputs.

* **Defaults:** By default, most `kthreads` are created with `SCHED_NORMAL` and a nice value of **0**.

### 4. How to Verify the Priorities

Once we compile and load the module (insmod), we can verify that the kernel is treating these threads differently using standard Linux tools:
1. Check dmesg: we will see logs from both, but the high-priority thread is guaranteed CPU time first by the scheduler.
2. Use `top` or `ps`:    
Run the following command to see the scheduling class (`CLS`) and priority (`PRI`):
```bash
ps -eo pid,cls,pri,ni,comm | grep _kthread
```
* High Prio: We’ll see `FF` (**FIFO**) or a very high priority number.
* Low Prio: We’ll see `TS` (**Time Sharing/CFS**) and a nice value (**NI**) of 19.
--------------------------------------

### ⚠️ Warning on `SCHED_FIFO`

If we use `msleep(500)` in the `.sched_priority = 95`, `SCHED_FIFO` kernel.
 thread.    
 that performs a "busy-wait" (a loop without sleeping), it will starve the CPU. Because it is higher priority than almost everything (including our shell), you won't be able to kill the process or even move our mouse. Always ensure RT threads yield or sleep!






