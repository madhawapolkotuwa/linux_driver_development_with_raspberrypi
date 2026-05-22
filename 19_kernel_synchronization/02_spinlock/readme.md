# Spinlock

## Video :-

[![Youtube Video](https://img.youtube.com/vi/tYRFo7ZgJxQ/0.jpg)](https://www.youtube.com/watch?v=tYRFo7ZgJxQ)

The mutex example showed how to protect shared data between two process-context callers.     
A mutex works there because both callers can sleep while waiting.

An ISR **cannot sleep**.     
When a GPIO interrupt fires, the kernel runs the ISR in interrupt context.
Sleeping in interrupt context crashes the kernel.    

`A spinlock solves this`.  
Instead of sleeping, the waiting CPU spins in a tight loop checking the lock until it is released.   
No sleeping. No scheduling. Just busy-waiting.

Rule: 
* Use `mutex` in process context (**can sleep**)
* Use `spinlock` in atomic/interrupt context (**cannot sleep**)

--------------
--------------
```
CPU 0 (process context)       CPU 1 (interrupt context - ISR)
───────────────────────       ───────────────────────────────

spin_lock(&my_lock)           /* Button pressed → ISR fires */
  ✓ Acquired                          │
        │                             ▼
        ▼                     spin_lock(&my_lock)
  /* modifies shared data */    ✗ Already held → SPINS (busy-waits)
        │
        ▼
spin_unlock(&my_lock)
                              ← lock released → spinning stops
                                      │
                                      ▼
                               /* modifies shared data */
                                      │
                                      ▼
                               spin_unlock(&my_lock)
```

While spinning, the CPU is fully occupied and cannot do useful work.

This is acceptable because spinlocks are **held for very short durations**  
a few instructions, never comes to milliseconds.

-----------------------
-----------------------

* How It Works     
A spinlock is a single integer ➞ locked or unlocked.

* `spin_lock()` ➞ acquire the lock. Spins until available.
* `spin_unlock()` ➞ release the lock.
* `spin_lock_irqsave()` ➞ acquire the lock and disable local interrupts, saving the previous interrupt state.
* `spin_unlock_irqrestore()` - release the lock and restore interrupts to the saved state.

When protecting data shared with an **ISR**, **always use** `spin_lock_irqsave()`.   
If you only use `spin_lock()` from process context, 
the **ISR** can fire on the same CPU while the lock is held, try to acquire it,
and spin forever ➞ deadlock.   
`spin_lock_irqsave()` prevents the ISR from firing on that CPU while the lock is held.

-------------------------

## Driver Implementation

### 1. Declare and Initialize the Spinlock
```c++
#include <linux/spinlock.h>

static spinlock_t my_spinlock;

static int __init my_init(void)
{
    spin_lock_init(&my_spinlock);
    /* ... rest of init ... */
}
```
Or statically with the macro:
```c++
static DEFINE_SPINLOCK(my_spinlock);
```

### 2. Protect Shared Data from Process Context
```c++
static ssize_t my_read(struct file *file, char __user *buf,
                       size_t count, loff_t *ppos)
{
    unsigned long flags;
    char local_copy[BUFFER_SIZE];
    int len;

    /* Acquire spinlock AND disable local interrupts.
       flags saves the current interrupt enable state. */
    spin_lock_irqsave(&my_spinlock, flags);

    /* --- Critical section --- */
    memcpy(local_copy, shared_buffer, shared_len);
    len = shared_len;
    /* --- End of critical section --- */

    /* Release spinlock AND restore interrupts to previous state */
    spin_unlock_irqrestore(&my_spinlock, flags);

    /* copy_to_user OUTSIDE the spinlock - it can sleep */
    return copy_to_user(buf, local_copy, len) ? -EFAULT : len;
}
```

Notice that `copy_to_user()` is done **outside the spinlock**.   
`copy_to_user()` can sleep (it may trigger a page fault).    
You must never sleep while holding a spinlock.   
Copy the data to a local buffer first, then release the lock, then copy to user space.

### 3. Protect Shared Data from the ISR
```c++
static irqreturn_t isr(int irq, void *dev_id)
{
    /* In interrupt context - use spin_lock(), NOT spin_lock_irqsave().
       IRQs are already disabled on this CPU when an ISR runs.
       Using irqsave here is redundant but not harmful. spin_lock() is fine. */
    spin_lock(&my_spinlock);

    /* --- Critical section --- */
    snprintf(shared_buffer, BUFFER_SIZE, "Button pressed!");
    shared_len = strlen(shared_buffer);
    /* --- End of critical section --- */

    spin_unlock(&my_spinlock);

    return IRQ_HANDLED;
}
```

-------------------------
-------------------------

```c++
Process Context (CPU 0)               Interrupt Context (CPU 0 or 1)
───────────────────────               ──────────────────────────────

spin_lock_irqsave(&lock, flags)
        │
        ▼
  Interrupts disabled on CPU 0
  Lock acquired
        │
        ▼
  /* copy data to local buffer */
        │
        ▼
spin_unlock_irqrestore(&lock, flags)
        │
        ▼
  Interrupts re-enabled
  Lock released
        │                             (Button pressed → ISR fires)
        ▼                                       │
  copy_to_user()                                ▼
  (safe - lock is released)           spin_lock(&lock)
                                        ✓ Acquired (no contention)
                                                │
                                                ▼
                                        /* update shared buffer */
                                                │
                                                ▼
                                        spin_unlock(&lock)
```


What Happens Without `irqsave` (the Deadlock Scenario)
```c++
CPU 0 - Process Context        CPU 0 - ISR (same CPU)
───────────────────────        ──────────────────────

spin_lock(&lock)
  ✓ Acquired
        │
        ▼
  /* inside critical section */
        │
        │   ← Interrupt fires on same CPU!
        │
        ▼
                               spin_lock(&lock)
                                 ✗ Already held by CPU 0
                                 → SPINS FOREVER
                                 (same CPU, process context
                                  never gets to unlock)

                               ← DEADLOCK
```
*spins forever*: because the same CPU cannot progress to release the lock.     
This is why `spin_lock_irqsave()` is mandatory when an ISR shares the lock.

### Important Notes
**Never sleep while holding a spinlock**    
Sleeping requires the scheduler to run. The scheduler cannot safely run while a spinlock is held.    
These operations are forbidden inside a spinlock:
```c++
spin_lock_irqsave(&my_lock, flags);

    copy_to_user(...)        /* ← may sleep on page fault */
    copy_from_user(...)      /* ← may sleep on page fault */
    kmalloc(GFP_KERNEL, ...) /* ← may sleep waiting for memory */
    mutex_lock(...)          /* ← sleeps */
    msleep(...)              /* ← sleeps */

spin_unlock_irqrestore(&my_lock, flags);
```

Always do the minimum work inside the `spinlock`. Copy data to a local variable, release the lock, then do the rest.

**Spinlocks are for short critical sections only**  
A spinlock that is held for milliseconds wastes an entire CPU core for that duration.
If the critical section is long, use a **[mutex](../01_mutex/)** instead.

A spinlock is appropriate when the critical section is a handful of instructions.(a copy, an increment, a flag update, etc).

**Priority Inversion**
Spinlocks do NOT support priority inheritance. But we know **Mutexes** DO.   
This means **spinlocks** can suffer from priority inversion issues.

**On a single-core system, spinlocks still matter for ISR safety**
On uniprocessor, `spin_lock()` compiles to nothing (there is no other CPU to spin).
`But spin_lock_irqsave()` still disables interrupts, which is the actual protection needed.  

`The Raspberry Pi 4` has 4 cores ➞ true SMP (Symmetric Multi-Processing ) ➞ so the spinning matters there too.

### Which Variant to Use

|      Call     |       When to use     |
----------------|-----------------------|
`spin_lock() / spin_unlock()` | Process context only, no ISR shares this lock
`spin_lock_irqsave() / spin_unlock_irqrestore()` | Process context + ISR share the lock ➞ most common in drivers
`spin_lock_irq() / spin_unlock_irq()` | Same as irqsave but assumes IRQs were enabled ➞ risky, avoid
`spin_lock_bh() / spin_unlock_bh()` | Process context + softirq/tasklet share the lock (no hard IRQ)



