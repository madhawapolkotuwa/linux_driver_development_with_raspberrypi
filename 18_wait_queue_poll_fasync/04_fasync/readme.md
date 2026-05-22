# Async Notification using fasync

## Video :-

[![Youtube Video](https://img.youtube.com/vi/bR2qCrmzH-o/0.jpg)](https://www.youtube.com/watch?v=bR2qCrmzH-o)


The previous examples covered two ways a process can wait for data from a device:

* **Blocking read**: the process calls `read()` and sleeps inside the driver until data arrives. ([Busy-wait](../01_busy_wait/) & [Wait queues](../02_wait_queue/))
* **poll / select**: the process sleeps monitoring multiple fds, wakes up when one becomes ready, then calls `read()`.([poll & select](../03_poll_select/))

In both cases, the process(user-space app) has to wait. It cannot do other work while waiting.

**Async notification** (via `fasync`) is different.   
The process registers interest in the device, then continues running normally. It does not sit in a blocking read, and it does not stay inside a poll loop waiting.   
In our example:    
When the button is pressed, the kernel delivers a `SIGIO` signal to the process.     
interrupting whatever the process is currently doing,     
and then the signal handler in user space can call `read()` to fetch the data from the driver.

|              |                           |
---------------|---------------------------|
Blocking read  | the process sleeps inside the driver.
poll / select  | process sleeps in the kernel’s polling infrastructure waiting for an event.
fasync / SIGIO | process does not wait in either place. It keeps running, and the kernel notifies it asynchronously using a signal when data is ready.
<br>

>>This is one of the **simplest asynchronous I/O** mechanisms available in Linux, especially for character drivers.

<br>
<br>


How It Works ( with our button ISR )
```c++
USER SPACE                          KERNEL SPACE
──────────────                  ──────────────────────────

fcntl(fd, F_SETOWN, getpid())
fcntl(fd, F_SETFL, O_ASYNC)  ──────►  my_fasync()
                                      fasync_helper() adds this
                                      fd to async_queue
        │
        ▼
signal(SIGIO, my_handler)
        │
        ▼
/* Process continues doing
   other work freely */
        │
        │                            (Button pressed → ISR fires)
        │                                      │
        │                                      ▼            
        │                             kill_fasync(&async_queue,SIGIO, POLL_IN) ← sends SIGIO to registered process
        │
        |
        ▼
  SIGIO delivered
  my_handler() runs
        │
        |
        ▼
      read() ────────────────────────►      my_read()
        |                                   copy_to_user()
        |                                   return sizeof(message)
        │
        ▼
  "Hello from Kernel!"
        │
        ▼
  /* Process resumes
     other work */
```

The process is never removed from the run queue. It runs continuously and is only briefly interrupted by the signal handler when data arrives.

## Driver-Side Implementation

### 1.  Add a `fasync_struct` pointer
```c++
/* Linked list of processes that registered for async notification on this device */
static struct fasync_struct *async_queue;
```

### 2. Implement the `.fasync` file operation
```c++
static int my_fasync(int fd, struct file *file, int on)
{
    /* fasync_helper manages adding/removing this fd from async_queue.
       on = 1 when O_ASYNC is set, on = 0 when it is cleared or file is closed. */
    return fasync_helper(fd, file, on, &async_queue);
}
```

Register it in `fops`:
```c++
static struct file_operations fops = {

    .fasync  = my_fasync,   /* <-- add this */
};
```

### 3.  Signal the queue from the ISR

```c++
static irqreturn_t isr(int irq, void *dev_id)
{
    pr_info("%s: GPIO interrupt occurred\n", my_device);

    kill_fasync(&async_queue, SIGIO, POLL_IN);    /* Sends SIGIO to async-registered processes */

    return IRQ_HANDLED;
}
```

### 4. Clean up in `.release`
When the file is closed, the fd must be removed from `async_queue.` Call `fasync_helper` with `on = -1`:
```c++
static int my_release(struct inode *inode, struct file *file)
{
    /* Remove this fd from the async notification list */
    my_fasync(-1, file, 0);

    pr_info("%s: device closed\n", my_device);
    return 0;
}
```

-----------
-----------
<br>


## Flow Diagram

fasync Registration Flow

```c++
  USER SPACE                          KERNEL SPACE
──────────────                  ──────────────────────────

fcntl(fd, F_SETOWN, getpid())
        │
        ▼
fcntl(fd, F_SETFL, O_ASYNC)
        │
        └──────────────────────────► my_fasync(fd, file, on=1)
                                          │
                                          ▼
                                    fasync_helper()
                                    Adds fd to async_queue
                                    (linked list of waiters)
        │
        ▼
signal(SIGIO, sigio_handler)
        │
        ▼
  /* Process runs freely */
```

fasync Notification Flow

```c++
USER SPACE                          KERNEL SPACE
──────────────                  ──────────────────────────

  /* Doing other work */
        │
        │                       (Button pressed → ISR fires)
        │                                  │
        │                                  ▼
        │                            kill_fasync(&async_queue, SIGIO, POLL_IN)
        │                                  │
        ▼                                  │
  SIGIO delivered ◄────────────────────────┘
        │
        ▼
  sigio_handler() runs
  (interrupts main loop)
        │
        └──────────────────────────► my_read()
                                     copy_to_user()
                                     return sizeof(message)
        │
        ▼
  printf("[SIGIO] ...")
        │
        ▼
  /* Main loop resumes
     from where it left off */
```

## Important Notes

`fasync_helper` **is called in two places**  
`.fasync` is called by the kernel both when `O_ASYNC` is set and when the file is closed.   
The `on` parameter tells `fasync_helper` what to do:
`on` value      |       meaning     
----------------|----------------
`1`               | Add this **fd** to the **async queue**
`0`               | Remove this **fd** from the **async queue**
`-1`              | Force removal, which is usually used during cleanup.

Always call `my_fasync(-1, file, 0)` in `.release`   
if the process exits without clearing `O_ASYNC`,    
this ensures the **fd** is removed from `async_queue` before the memory is freed.    
Failing to do this causes a **use-after-free kernel crash**.

### Signal handler limitations
Signal handlers run in a restricted context.
The process is interrupted at an arbitrary point.    
Keep signal handlers **short and simple:**
* Safe: `read()`, `write()`, `printf()` with care, setting a flag.
* Problematic: calling non-reentrant library functions, acquiring mutexes, or doing complex work.

A common production pattern is to set a simple flag in the handler and do the actual `read()` in the main loop:
```c++
static volatile sig_atomic_t data_ready = 0;

static void sigio_handler(int sig)
{
    data_ready = 1;   /* Just set a flag - main loop does the read */
}

/* In main loop: */
if (data_ready) {
    data_ready = 0;
    read(fd, buffer, sizeof(buffer));
}
```

kill_fasync vs wake_up_interruptible
Both can coexist in the ISR: they serve different audiences:
call                | Who it notifies
--------------------|----------------
`wake_up_interruptible(&my_wait_queue)` | Processes sleeping in `read()` or `poll()/select()`
`kill_fasync(&async_queue, SIGIO, POLL_IN)` | Processes registered for async notification via `O_ASYNC`

A process using async notification does not need to be in the wait queue at all,     
it is simply running and will be interrupted by the signal.
