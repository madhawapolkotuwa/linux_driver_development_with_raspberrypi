
## Blocking vs Non-Blocking I/O

### Video :-
[![Youtube Video](https://img.youtube.com/vi/q2r0_LYCYvc/0.jpg)](https://www.youtube.com/watch?v=q2r0_LYCYvc)

Before understanding `wait queues`, we must clearly understand what **blocking** and **non-blocking I/O** mean inside a Linux driver.

---

## What is Blocking I/O?

**Blocking I/O** means that if data is not available when a process calls `read()`, the process will sleep until data becomes available.     
This is the default behavior in Linux - the kernel suspends the process, freeing up CPU resources until the driver wakes it up.


Example:

```c++
read(fd, buffer, size);
```

If the driver has no data ready,     
the process will go to sleep inside the driver and will not return until data is available.

---

## What is Non-Blocking I/O?

**Non-blocking I/O** means the system call `returns immediately`,    
regardless of whether data is available or not.

- If data is available → return the data.
- If data is not available → return `-EAGAIN` (errno: "Resource temporarily unavailable").

The application is responsible for deciding what to do next - typically by polling or using `select()`/`poll()`/`epoll()`.

---

### The `O_NONBLOCK` Flag

When a process opens a device with `O_NONBLOCK`, or sets it later via `fcntl()`,    
every `read()`/`write()` that would normally block must instead return `-EAGAIN` immediately.

## How User Space Enables Non-Blocking Mode

```c++
#include <fcntl.h>

/* Method 1: Set flag on an already-open file descriptor */
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);

/* Method 2: Set flag at open time */
int fd = open("/dev/mydev", O_RDONLY | O_NONBLOCK);
```

Once set, `read()` will never sleep - it returns immediately.

---

## Driver-Side Implementation

Inside the driver's `.read()` callback, we check the `O_NONBLOCK` flag by inspecting `file->f_flags`:

```c++
static int data_available = 0;

static ssize_t my_read(struct file *file,
                       char __user *buf,
                       size_t len,
                       loff_t *off)
{
    if (!data_available) {

        /* Check if the file descriptor was opened in non-blocking mode */
        if (file->f_flags & O_NONBLOCK) {
            printk(KERN_INFO "Non-blocking read - no data available\n");
            return -EAGAIN; /* Return immediately with "try again" error */
        }

        /* Blocking mode: busy-wait until data becomes available */
        printk(KERN_INFO "Blocking read - process sleeping...\n");

        /* NOTE: This is a placeholder - wait queues will replace this */
        while (!data_available)
            cpu_relax();
    }

    data_available = 0;

    copy_to_user(buf, "Hello\n", 6);
    return 6;
}
```

---

## What Happens Internally?

Blocking Mode:


User → read() → Driver → data_available == 0 → Sleep → ISR sets data_available = 1 → Wake up → Return data


Non-Blocking Mode:

User → read() → Driver → data_available == 0 → Return -EAGAIN immediately

---


1. Load driver
2. Open device normally
3. Call `read()`
   → Terminal hangs (blocking)

-------------------------------------
#### Full Driver Code:    
**`blocking_nonblocking.c`**


### test app     
**`test_nonblock.c`**

-----------------------------------

## ⚠️ Important: Why Busy-Waiting is Wrong
The current implementation uses a busy-wait loop, which is incorrect for production drivers:
```c++
while (!data_available)
    cpu_relax();   /* Burns 100% of one CPU core */
```
This approach:

* Wastes CPU cycles - the process consumes an entire core doing nothing useful.
* Cannot be interrupted by signals (e.g., `Ctrl+C` or `kill -9` will not work while spinning).
* Prevents the kernel from removing the module (`rmmod` will fail with "Resource temporarily unavailable").

Correct solution → Wait Queues

Wait queues allow the process to `truly sleep` (state `S` - interruptible) while consuming zero CPU, and wake up only when the ISR signals that data is ready. This will be covered in the next section.


## Test Result

### Test 1: Non-Blocking Read
#### *Step 1* - Load the module
```bash
sudo insmod blocking_nonblocking.ko
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

Because the file was opened with `O_NONBLOCK` and `data_available == 0`, the driver immediately returns `-EAGAIN` without sleeping.


#### *Step 3* - Press the hardware button

The falling edge on GPIO 20 triggers the ISR:
```
my_cdev: GPIO Interrupt occoured
```
Now `data_available = 1`.

#### *Step 4* - Run the test app again
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
Because `data_available == 1`, the driver skips the blocking check,
resets `data_available = 0`, copies the message to user space,  and returns.

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

What happens:

`data_available == 0` → my_read enters blocking mode and spins in `cpu_relax()`.
 ```c++
 while (!data_available) 
            cpu_relax(); /* will take 100% cpu */
 ```
#### *Step 2* - Press the hardware button
Press the button → ISR sets `data_available = 1`.

The busy-wait loop exits, 

reset `data_available = 0`,

the message is copied to user space, and `cat` prints:
```
Hello from Kernel!
```
cat immediately calls `read()` again → back to spinning.

-----
 
#### The problem - you cannot interrupt the process:
Try pressing `Ctrl+C`. Nothing happens. Run `top` and you will see the `cat` process consuming **100% CPU** with state `R` (running - actively spinning on the CPU).


```bash
top
```

```
PID   USER   PR  NI  VIRT  RES  S  %CPU  COMMAND
1234  root   20   0   ...  ...  R  100   cat
```

Now, if I force the removal of the module.

```
sudo rmmod -f blocking_nonblocking
```

Because the process holds an open file descriptor to the device, the reference count on the module is non-zero. The kernel refuses to unload it. A forced `rmmod -f` will also fail while the process is actively spinning in the driver.

**Root cause**: The busy-wait loop never returns control to the kernel scheduler, so signals, module unloading, and process cleanup are all deferred indefinitely.

how to terminate the process:

One way `reboot` the system.

other way: 

first identify the process ID, run:

```
top
```

Identify the cat PID.

Check Process State:

```
ps -lp <PID>
```

S (State) column shows the state currently. State is  R, R means running (The process is spinning on the CPU)

then run:

```
sudo kill -9 <PID>
```

nothing will happen.

If we check again, Check Process State:

```
ps -lp <PID>
```
still state is R.

Now, if I press the button again.
In the console, we can see the processed killed:

And also dmesg, we can see.

```
my_cdev: device closed
```

So process terminated, file closed.

### Why `kill -9` does not work immediately:

Even `SIGKILL` cannot interrupt a process that is spinning inside the kernel. The signal is delivered, but the process cannot act on it until it returns from kernel space. Since the busy-wait loop never yields, the process stays in `R` state indefinitely.

The only way to unblock it is to press the button - this allows the driver to return from `my_read`, at which point the kernel delivers the pending signal and terminates the process.


# Summary


|       | Blocking I/O | Non-Blocking I/O |
--------|--------------|------------------|
**Behavior when no data** | Process sleeps | Returns `-EAGAIN` immediately 
**CPU usage while waiting** | 0% (with wait queues) | Depends on app polling strategy
**Default mode** | ✅ Yes | Set via `O_NONBLOCK`
**Typical use case** | Simple sequential reads | Event-driven / async applications

**Key takeaway:** Blocking I/O with proper wait queues is the efficient, correct approach. Busy-waiting is never acceptable in real kernel drivers - it wastes CPU, blocks signals, and prevents clean module unloading.

**Next: Wait Queues** - replacing `cpu_relax()` with `wait_event_interruptible()` for correct, signal-safe blocking behavior.
