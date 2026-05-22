# poll and select

## Video :-

[![Youtube Video](https://img.youtube.com/vi/PlkS1O_GekE/0.jpg)](https://www.youtube.com/watch?v=PlkS1O_GekE)

The `poll` and `select` mechanisms are user-space system calls that allow a single process to **monitor multiple file descriptors at the same time**.

Instead of waiting on just one file descriptor,       
a process can give the kernel a list of file descriptors and ask to be notified when **any one of them becomes ready for I/O**.

This allows the process to **sleep efficiently until an event occurs**,       
without busy-waiting and without creating a separate thread for each file descriptor.

This is the standard design used in **event-driven applications**.

Instead of blocking on `read()`: which waits on only one file descriptor or continuously retrying operations that return `-EAGAIN`,       
the application simply passes a set of file descriptors to the kernel and sleeps until at least one of them becomes ready.

From the driver side, both `poll` and `select` are implemented using the same mechanism.
They rely on the `.poll` file operation,   
which is typically implemented using a wait queue to notify the kernel when the device becomes ready.



```
User space                        Kernel space
──────────                        ────────────

poll(fds, nfds, timeout)
        │
        ▼
   For each fd in fds  ─────────► driver's .poll() called
                                        │
                                        ▼
                               poll_wait(file, &my_wait_queue, wait)
                               (registers the wait queue - does NOT sleep yet)
                                        │
                                        ▼
                               return current ready mask
                                  (0 if nothing ready)
        │
        ▼
   Any fd ready?
   ┌────┴─────┐
  YES         NO
   │           │
   ▼           ▼
 return      Sleep - wait for wake_up_interruptible()
 to app            │
                   │   (Button pressed → ISR fires or stdin)
                   │
                   ▼
             .poll() called again for each fd
             Returns non-zero mask → fd is ready
                   │
                   ▼
             poll() returns to user space
                   │
                   ▼
             App calls read() - data is available
```

`poll_wait()` does not put the process to sleep by itself.  
It only registers the **wait queue** so the kernel knows where to wake the process from.    
The kernel's `poll` infrastructure handles the actual **sleeping and waking**.

-------

**Return Mask Values**

Flag            |       Meaning
----------------|--------------------------------------
`EPOLLIN`       |       Data available for reading
`EPOLLRDNORM`   |       Normal data available (same as `EPOLLIN`, included for POSIX compatibility)
`EPOLLOUT`      |       Space available for writing
`EPOLLERR`      |       Error condition
`EPOLLHUP`      |       Hang-up (device disconnected)
`0`             |       No events ready - process will sleep

For a read-only device like ours, returning `EPOLLIN | EPOLLRDNORM` is the standard combination.

The ISR  (Button ISR)

The ISR is identical to the wait queue example. `wake_up_interruptible()` wakes both `wait_event_interruptible()` callers and `poll/select` waiters  they all share the same wait queue:


## `poll` vs `select` - What Is the Difference?
Both system calls do the same job. The driver implementation is **identical**    
the same `.poll` callback serves both. The difference is only in the **user-space API**.

|                   |   `select`    |   `poll`    |
-------------------|---------------|-------------|
**fd sets** | Three separate `fd_set` bitmasks (read, write, except) | Single array of `struct pollfd` |
**Max fds** | Limited by `FD_SETSIZE` (usually 1024) | No hard limit
**fd sets modified** | Yes - must be rebuilt before every call | No - `revents` field is separate from `events`
**Timeout type** | `struct timeval` (microseconds) | `int` milliseconds
**Preferred for new code** | No | Yes - more flexible


`poll` is generally preferred in new code.  
`select` is older and has the `FD_SETSIZE` limitation, but is still widely used and supported everywhere.





