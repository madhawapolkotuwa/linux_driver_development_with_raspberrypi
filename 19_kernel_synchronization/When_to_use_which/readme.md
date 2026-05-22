# When to Use Which

Three synchronization primitives are now covered: `mutex`, `spinlock`, and `completion`.     

They are not interchangeable. Each solves a specific problem.   

Choosing the wrong one either crashes the kernel or produces subtle data corruption.

This guide makes the decision mechanical.

---------------

The One-Question Filter
The first question to ask is not "which is faster?" 
 
it is:
>> Where is the code that needs protection running?

```
 Can the code sleep?
        │
   ┌────┴─────┐
  YES          NO
   │            │
   ▼            ▼
 Mutex       Spinlock
```

⊛　If the code is in interrupt context (ISR, tasklet, softirq) ➡ it cannot sleep.  
Use a **spinlock**.

⊛　If the code is in process context (file operations, kernel threads, syscalls) ➡ it can sleep.     
Use a **mutex**.     

⊛　If the question is not "protect shared data" but "wait for something to finish"   
Use a **completion**.

## Decision Table

| Situation   | Primitive  | Reason
|-------------|------------|--------
Two processes call `read()`/`write()` simultaneously | Mutex | Both are in process context, can sleep    
ISR updates a buffer that `read()` also accesses | Spinlock (`irqsave`) | ISR cannot sleep; must also disable IRQs on the locking CPU   
Kernel thread waits for an ISR to complete an operation | Completion | Thread sleeps efficiently; ISR calls `complete()`     
Protecting a counter touched from two kernel threads | Mutex | Both threads can sleep; mutex is simpler  
Protecting a flag updated in a tasklet | Spinlock (`bh`) | Tasklet is softirq context ➡ cannot sleep   　
One-time initialization: thread B waits for thread A to finish setup | Completion | Ordering signal, not a lock 
Timeout waiting for hardware to respond | Completion (`timeout` variant) | Clean timeout handling built in  


## Comparison

|    | Mutex | Spinlock | Completion
-----|-------|----------|------|
Waiter behaviour | Sleeps | Spins (busy-waits) | Sleeps  
Usable in ISR   |✗ No | ✓ Yes | ✓ Yes (`complete()` only)     
Usable in process context | ✓ Yes | ✓ Yes | ✓ Yes     
Purpose | Mutual exclusion | Mutual exclusion | Signaling / ordering     
Owner concept | Yes - locker must unlock | ✗ No | ✗ No     
Can be held across sleep | ✓ Yes | ✗ No | N/A  
Overhead | Higher (context switch) | Lower (no switch) but wastes CPU | Low  
Best for | Long critical sections | Very short critical sections (< few µs) | Thread ↔ thread / ISR ↔ thread signaling   


```
Need mutual exclusion?
        │
   ┌────┴──────────────────────────────┐
   │                                   │
   ▼                                   ▼
ISR or atomic context?          Process context only?
   │                                   │
   ▼                                   ▼
SPINLOCK                            MUTEX
(spin_lock_irqsave if               (mutex_lock_interruptible
 ISR shares the lock)                in file operations)

----------------------------------------------------------------

Need to wait for something to finish?
        │
        ▼
COMPLETION
(complete() from ISR or thread,
 wait_for_completion_interruptible_timeout() for safety)
```



