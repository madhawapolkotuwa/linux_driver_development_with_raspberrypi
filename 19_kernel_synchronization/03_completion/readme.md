# Completion

## Video :- 
[![Youtube Video](https://img.youtube.com/vi/jaLk6viee7g/0.jpg)](https://www.youtube.com/watch?v=jaLk6viee7g)

The `mutex` and `spinlock` examples protected shared data from concurrent access.    
Both are about **locking** ➞ keeping one caller out while another is inside.    

A `completion` solves a different problem: **signaling between threads**.    
One thread does work. Another thread waits for that work to finish.
The waiting thread should sleep efficiently 
- not spin 
- not poll 
- just sleep until it is told to wake up.

This is the kernel equivalent of a one-shot event flag.

```c
Thread A (waits)                    Thread B (does the work)
────────────────                    ────────────────────────

init_completion(&my_comp)

        │
        ▼
wait_for_completion(&my_comp)
  → sleeps immediately
        │                           /* ... does some work ... */
        │                                       │
        │                                       ▼
        │                           complete(&my_comp)
        │                                       │
        ◄───────────────────────────────────────┘
        │
        ▼
  /* wakes up, work is done */
```
`wait_for_completion()` puts the thread to sleep.  
`complete()` wakes it up.  
No polling. No shared flag to check manually. No wait queue to manage.   
The completion handles all of it internally.     

--------------------------

### How It Works

A completion contains:

* **A counter** ➞ how many complete() calls are pending.
* **A wait queue** ➞ where threads sleeping on this completion live.

`complete()` increments the counter and wakes one waiter.  
`complete_all()` wakes all waiters.    
`wait_for_completion()` decrements the counter if it is non-zero, or sleeps until it is.   
This means `complete()` can be called before `wait_for_completion()`.   
The waiter will not sleep ➞ it sees the counter is already non-zero and returns immediately.     
This is the key advantage over a raw wait queue: ordering does not matter.   

-----------------

### Driver-Side Implementation

1. Declare and Initialize
```c++
#include <linux/completion.h>

static struct completion my_comp;

static int __init my_init(void)
{
    init_completion(&my_comp);
    /* ... rest of init ... */
}
```
Or statically with the macro:

```c
static DECLARE_COMPLETION(my_comp);
```

2. Wait for the Completion (Reader Thread)
```c++
static int reader_thread(void *data)
{
    pr_info("reader: waiting for data...\n");

    /* Sleep until complete() is called.
       Returns 0 if interrupted by a fatal signal. */
    if (!wait_for_completion_interruptible(&my_comp)) {
        pr_info("reader: interrupted by signal\n");
        return -EINTR;
    }

    pr_info("reader: got the data - processing...\n");
    /* ... consume the result ... */

    return 0;
}  
```

3. Signal the Completion (ISR or Writer Thread)  
From an ISR (interrupt context - complete() is safe here):
```c++
static irqreturn_t isr(int irq, void *dev_id)
{
    pr_info("ISR: operation finished\n");

    /* Wake up whoever is waiting on my_comp.
       complete() is safe in interrupt context - it does not sleep. */
    complete(&my_comp);

    return IRQ_HANDLED;
}
```
Or from a kernel thread (process context):
```c++
static int writer_thread(void *data)
{
    /* ... do some work ... */
    pr_info("writer: work done, signaling reader\n");

    complete(&my_comp);   /* wake up the reader */
    return 0;
}
```

4. Timeout Variant
```c++
unsigned long timeout = msecs_to_jiffies(5000);   /* 5 seconds */

long ret = wait_for_completion_timeout(&my_comp, timeout);

if (ret == 0) {
    pr_err("timed out waiting for completion\n");
    return -ETIMEDOUT;
}

pr_info("completed with %ld jiffies to spare\n", ret);
```

`wait_for_completion_timeout()` returns the number of jiffies remaining.     
If it returns 0, the timeout expired before `complete()` was called.

-----------
-----------
<br>

```c++
Thread A                                                   Thread B / ISR
────────                                                   ──────────────

init_completion(&my_comp)
  counter = 0
        │
        ▼
wait_for_completion()
        │
        ▼
  counter == 0?
  ┌─────┴──────┐
 YES (=0)      NO
  │             │
  ▼             ▼
 Sleep        Return immediately
 on wait      (counter--)
 queue
        │
        │                                                     complete(&my_comp)
        │                                                           │
        │                                                           ▼
        │                                                     counter++
        │                                                     wake up Thread A
        │
        ▼
  Thread A wakes
  counter--
  continues execution
```

## Race-Safe Flow (complete before wait)

```c++
Thread B / ISR                              Thread A
──────────────                              ────────

complete(&my_comp)
  counter = 1
  (no waiters yet - nothing to wake)

                                        wait_for_completion()
                                        counter == 1 → not zero
                                        counter-- → 0
                                        returns immediately (no sleep)
```

This ordering safety is what makes completions superior to a hand-rolled flag + wait queue.

----------------------

#### Important Notes
`complete()` is safe in interrupt context    
`Unlike mutex_lock()`, `complete()` never sleeps. It can be called from an ISR,  
a tasklet, a softirq, or process context.    
`complete_all()` wakes every waiter    
If multiple threads are sleeping on the same completion, `complete()` wakes only one.    
`complete_all()` wakes all of them and sets the counter to the maximum value     
so that any future `wait_for_completion()` calls also return immediately.
```c++
complete(&my_comp);      /* wake one waiter */
complete_all(&my_comp);  /* wake all waiters + any future callers */
```

Do not use a **completion** as a **mutex**   
A completion is not a lock. It is a one-directional signal.  
Using `wait_for_completion()` + `complete()` to guard a critical section is a design error,      
use a `mutex` or `spinlock` for that.    

#### Variants :

|       Call                            |                 Behaviour                      |
|---------------------------------------|------------------------------------------------|
| `wait_for_completion()`               | Sleeps unconditionally → cannot be interrupted |
| `wait_for_completion_interruptible()` | Wakes on any signal → returns 0 if interrupted |
| `wait_for_completion_killable()`      | Wakes only on fatal signals (**SIGKILL**)      |
| `wait_for_completion_timeout()`       | Wakes on completion or timeout → returns remaining jiffies (0 = timed out) |
| `wait_for_completion_interruptible_timeout()` | Timeout + signal interruptible |

Prefer `wait_for_completion_interruptible()` or the timeout variant in drivers   
so that a stuck operation does not permanently freeze a user process.



