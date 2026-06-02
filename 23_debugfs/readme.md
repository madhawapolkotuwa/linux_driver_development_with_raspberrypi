# Debugfs for Driver Debugging

## Video :-

[![Youtube Video](https://img.youtube.com/vi/DcJwAmhwFGQ/0.jpg)](https://www.youtube.com/watch?v=DcJwAmhwFGQ)

Overview
Debugfs is a RAM-based virtual filesystem designed specifically for kernel
developers to expose driver internals to user space during development and
debugging.   
Unlike `sysfs` - which has strict one-value-per-file rules and
must maintain a stable `ABI`(Application Binary Interface) - debugfs has no rules at all. We can put whatever we want there.

```
debugfs  - no rules · for developers only · not a stable ABI
sysfs    - one value per file · stable ABI · user-facing
procfs   - process information only · not for driver data
```

The key difference in practice:

|          | debugfs | sysfs |
|----------|---------|-------|
Purpose | Driver debugging | User-facing device interface
ABI stability | None - can change any time | Expected to be stable
Rules | No rules | One value per file
Who reads it | Developer tools | User space applications
Mounted at | `/sys/kernel/debug/` |  `/sys/`

>> Important: The debugfs API is exported GPL-only. our module must use `MODULE_LICENSE("GPL")`.

------------

### 1. Mounting Debugfs  

On most modern Linux distributions, debugfs is already mounted automatically.
On Raspberry Pi, verify with:
```bash
ls /sys/kernel/debug/
```

If it is not mounted, mount it manually:
```bash
mount -t debugfs none /sys/kernel/debug
```
Or add it to `/etc/fstab` for automatic mounting at boot:
```bash
debugfs    /sys/kernel/debug    debugfs    defaults    0    0
```
Access to the debugfs root is restricted to root by default.

------

### 2. Creating a Debugfs Directory
The first thing our driver does is create a directory to hold its entries.
Always create a subdirectory - never dump files directly into the debugfs root.

```c
#include <linux/debugfs.h>

static struct dentry *dbg_dir;

static int __init mydrv_init(void)
{
    /* creates /sys/kernel/debug/mydrv/ */
    dbg_dir = debugfs_create_dir("mydrv", NULL);

    if(IS_ERR(dbg_dir))
        return PTR_ERR(dbg_dir);

    return 0;
}

static void __exit mydrv_exit(void)
{
    /* removes the directory AND everything inside it recursively */
    debugfs_remove(dbg_dir);
}
```
`debugfs_create_dir()` returns a `struct dentry *`. We store this and pass
it as the parent argument when creating files inside the directory.  

**Critical rule**: There is no automatic cleanup. If our module unloads
without calling `debugfs_remove()`, the result is stale pointers and kernel
instability. Always call `debugfs_remove()` in our exit function.   

`debugfs_remove()` is recursive - one call on the directory removes
everything inside it. We do not need to remove files individually.

-----

### 3. Variable Helpers - No fops Needed

For exposing simple variables, debugfs provides typed helper functions that
create a file with built-in read/write support. We pass a pointer directly
to our variable - no `file_operations` required.

**Integer types (decimal)**

```c
static u32 my_counter = 0;
static u32 my_threshold = 100;

/* In probe() or init: */
debugfs_create_u32("counter",   0444, dbg_dir, &my_counter);
debugfs_create_u32("threshold", 0644, dbg_dir, &my_threshold);
```
Full set of decimal integer helpers:
```c
void debugfs_create_u8   (const char *name, umode_t mode, struct dentry *parent, u8   *value);
void debugfs_create_u16  (const char *name, umode_t mode, struct dentry *parent, u16  *value);
void debugfs_create_u32  (const char *name, umode_t mode, struct dentry *parent, u32  *value);
void debugfs_create_u64  (const char *name, umode_t mode, struct dentry *parent, u64  *value);
void debugfs_create_ulong(const char *name, umode_t mode, struct dentry *parent, unsigned long *value);
```

**Integer types (hexadecimal)**  
When the value is a register or address, hex is more readable:
```c
static u32 reg_status = 0xDEADBEEF;

debugfs_create_x32("reg_status", 0444, dbg_dir, &reg_status);
```

```c
void debugfs_create_x8 (const char *name, umode_t mode, struct dentry *parent, u8  *value);
void debugfs_create_x16(const char *name, umode_t mode, struct dentry *parent, u16 *value);
void debugfs_create_x32(const char *name, umode_t mode, struct dentry *parent, u32 *value);
void debugfs_create_x64(const char *name, umode_t mode, struct dentry *parent, u64 *value);
```

**Boolean**
```c
static bool debug_enabled = false;

debugfs_create_bool("debug_enabled", 0644, dbg_dir, &debug_enabled);
```
Reading returns Y or N. Writing accepts Y, N, 1, or 0.

**Binary blob**
```C
static struct debugfs_blob_wrapper my_blob = {
    .data = my_data_buffer,
    .size = sizeof(my_data_buffer),
};

debugfs_create_blob("raw_data", 0444, dbg_dir, &my_blob);
```
Note: `debugfs_create_blob()` creates a read-only file regardless of the
mode bits.

**atomic_t**
```C
static atomic_t irq_count = ATOMIC_INIT(0);

debugfs_create_atomic_t("irq_count", 0444, dbg_dir, &irq_count);
```

### 4. Custom Files - debugfs_create_file()
For anything beyond a single variable - formatted output, multiple fields,
write-side validation - `use debugfs_create_file()` with our own
`file_operations`.

```c
struct dentry *debugfs_create_file(const char *name,
                                   umode_t mode,
                                   struct dentry *parent,
                                   void *data,
                                   const struct file_operations *fops);
```
The `data` pointer is stored in `inode->i_private` and retrieved in our
`open()` callback - this is how we get our driver's private data back.  

**Simple custom file**
```c
static int mydrv_stats_show(struct seq_file *s, void *unused)
{
    struct mydrv_dev *dev = s->private;

    seq_printf(s, "irq_count:  %u\n", dev->irq_count);
    seq_printf(s, "tx_bytes:   %llu\n", dev->tx_bytes);
    seq_printf(s, "rx_bytes:   %llu\n", dev->rx_bytes);
    seq_printf(s, "errors:     %u\n", dev->error_count);
    seq_printf(s, "state:      %s\n",
               dev->running ? "running" : "stopped");

    return 0;
}

static int mydrv_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, mydrv_stats_show, inode->i_private);
}

static const struct file_operations stats_fops = {
    .owner   = THIS_MODULE,
    .open    = mydrv_stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* In probe() or init: */
debugfs_create_file("stats", 0444, dbg_dir, dev, &stats_fops);
```

`single_open()` + `seq_read` + `single_release` is the standard pattern
for a read-only debugfs file with formatted output. `seq_printf()` works
like printf but writes into the seq_file buffer which handles pagination
automatically.

**Writable custom file**
```c
static ssize_t mydrv_reset_write(struct file *file,
                                  const char __user *buf,
                                  size_t count, loff_t *ppos)
{
    struct mydrv_dev *dev = file->private_data;

    /* reset all counters */
    dev->irq_count  = 0;
    dev->tx_bytes   = 0;
    dev->rx_bytes   = 0;
    dev->error_count = 0;

    dev_info(&dev->pdev->dev, "debugfs: counters reset\n");
    return count;
}

static const struct file_operations reset_fops = {
    .owner = THIS_MODULE,
    .open  = simple_open,   /* simple_open stores inode->i_private in file->private_data */
    .write = mydrv_reset_write,
    .llseek = noop_llseek,
};

/* In probe() or init: */
debugfs_create_file("reset", 0200, dbg_dir, dev, &reset_fops);
```

`simple_open()` is a kernel helper that does one thing - it copies
`inode->i_private` into `file->private_data` so our write handler can
reach it. Use it for write-only files where we don't need `seq_file`.

**Register dump using regset32**     
For dumping a block of hardware registers:
```c
static const struct debugfs_reg32 mydrv_regs[] = {
    { "CTRL",   0x00 },
    { "STATUS", 0x04 },
    { "IRQ",    0x08 },
    { "DATA",   0x0C },
};

static struct debugfs_regset32 mydrv_regset = {
    .regs  = mydrv_regs,
    .nregs = ARRAY_SIZE(mydrv_regs),
    .base  = NULL,   /* set to dev->base (ioremap'd address) in probe */
};

/* In probe(): */
mydrv_regset.base = dev->base;
debugfs_create_regset32("registers", 0444, dbg_dir, &mydrv_regset);
```

Reading `/sys/kernel/debug/mydrv/registers` prints all register names and
their current values - useful for hardware debugging without needing a
custom read function.

-----------

### 5. Live Introspection of Driver Variables
The power of debugfs is live access to driver internals while the system
is running. Because the helper functions take a direct pointer to our
variable, the value in the file always reflects the current state - no
caching, no snapshots.
```c
struct mydrv_dev {
    u32           irq_count;
    u32           error_count;
    bool          debug_verbose;
    u32           reg_ctrl;

    struct dentry *dbg_dir;
    /* ... */
};

static int mydrv_probe(struct platform_device *pdev)
{
    struct mydrv_dev *dev;

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    /* ... init hardware ... */

    /* create debugfs entries - live pointers into the struct */
    dev->dbg_dir = debugfs_create_dir("mydrv", NULL);

    debugfs_create_u32 ("irq_count",    0444, dev->dbg_dir, &dev->irq_count);
    debugfs_create_u32 ("error_count",  0444, dev->dbg_dir, &dev->error_count);
    debugfs_create_bool("debug_verbose",0644, dev->dbg_dir, &dev->debug_verbose);
    debugfs_create_x32 ("reg_ctrl",     0444, dev->dbg_dir, &dev->reg_ctrl);
    debugfs_create_file("stats",        0444, dev->dbg_dir, dev, &stats_fops);

    return 0;
}

static int mydrv_remove(struct platform_device *pdev)
{
    struct mydrv_dev *dev = platform_get_drvdata(pdev);

    debugfs_remove(dev->dbg_dir);   /* removes everything recursively */

    return 0;
}
```

From user space, while the driver is running:
```bash
# Read current IRQ count - live value
cat /sys/kernel/debug/mydrv/irq_count

# Enable verbose debug logging
echo Y > /sys/kernel/debug/mydrv/debug_verbose

# Check hardware register value
cat /sys/kernel/debug/mydrv/reg_ctrl

# Read multi-line stats
cat /sys/kernel/debug/mydrv/stats

# See everything in the directory
ls /sys/kernel/debug/mydrv/
```

### 6. Differences from Sysfs
Both `sysfs` and `debugfs` expose driver data through the filesystem, but they
serve different purposes and have different rules.

**When to use `sysfs`**

* The interface is for user space applications - not just developers
* The file represents a device property - name, state, configuration
* The interface must remain stable across kernel versions
* We are following the Linux Device Model - attributes on a registered device

**When to use `debugfs`**

* The interface is for debugging during development - not production
* We want to expose internal counters, state machines, or register dumps
* We need multiple fields in one file (sysfs prohibits this)
* We want to quickly add and remove debug hooks without designing an API
* The data is volatile - meaningful now but irrelevant once the bug is fixed

**The `sysfs` one-value-per-file rule**    
`Sysfs` enforces that each file contains exactly one value. This is a design
constraint, not just a convention. `Debugfs` has no such rule - we can
`seq_printf` an entire page of formatted output into a single `debugfs` file.

```bash
sysfs:   /sys/class/mysensor/mysensor0/value  → "12\n"
debugfs: /sys/kernel/debug/mydrv/stats       → multiple lines, any format
```

`Stability`    
`Sysfs` attributes are expected to maintain a stable ABI - user space tools
depend on them and they should not change. `Debugfs` entries can be added,
removed, or reformatted freely between kernel versions. They are for
developers, not for scripting.

-----

7. Checking if `Debugfs` is Compiled In
If the kernel is built without `CONFIG_DEBUG_FS`, `debugfs_create_dir()`
returns Error. The helper functions (`debugfs_create_u32`, etc.)
silently do nothing and return a dummy dentry. Our driver should handle
this gracefully:

```c
dbg_dir = debugfs_create_dir("mydrv", NULL);

/* Do not treat ENODEV as a fatal error -
   debugfs just isn't available on this kernel build.
   The driver still works; it just has no debug interface. */
if (IS_ERR(dbg_dir))
    dev_warn(&pdev->dev, "debugfs not available\n");
```

For the helper functions, no error check is needed - they silently succeed
even when debugfs is not available.

------

### 8. Full Debugfs Tree
After loading a driver that uses all the mechanisms above:

```bash
/sys/kernel/debug/
└── mydrv/
    ├── irq_count       ← debugfs_create_u32  (decimal, read-only)
    ├── error_count     ← debugfs_create_u32  (decimal, read-only)
    ├── debug_verbose   ← debugfs_create_bool (Y/N, read-write)
    ├── reg_ctrl        ← debugfs_create_x32  (hex, read-only)
    ├── registers       ← debugfs_create_regset32 (all regs formatted)
    ├── stats           ← debugfs_create_file + seq_file (multi-line)
    └── reset           ← debugfs_create_file + write only
```


<br>
<br>

------------------
------------------
------------------



## Example

A single module that combines everything covered in Tutorials 21(`LDM`), 22(`sysfs`), and 23(`debugfs`)
into one complete driver:

Layer | What it creates | Path
------|-----------------|------
Character device | `/dev/mydrv0` | readable and writable
class interface | `/sys/class/mydrv/mydrv0/` | udev creates `/dev` automatically
Sysfs attributes | `name` (read-only), `value` (read-write) | under the class device
Debugfs directory | `/sys/kernel/debug/mydrv/` | live couners + status
Debugfs variables | `open_count`, `read_count`, `write_count`, `error_count`, `reg_status` | direct poniter into private data
Debugfs bool | `debug_verbose` | enables verbose kernel log output
Debugfs custom file | `stats` | multi-line formatted output via seq_file
Debugfs write file | `reset` | resets all counters to zero

#### File Structure
```
mydrv.c     ← single driver module - five clearly labelled sections
Makefile
```

### Code Structure

#### Section 1 - Character device file operations
The kernel buffer `kbuf` is the backing store for the character device.
`read()` copies from `kbuf` to user space. `write()` copies from user space
into `kbuf`. Every operation increments the matching counter and checks
`debug_verbose` to decide whether to log.

```c
struct mydrv_dev {
    char    kbuf[BUF_SIZE];   /* kernel buffer - backing store for /dev/mydrv0 */
    u32     open_count;       /* incremented in open() */
    u32     read_count;       /* incremented in read() */
    u32     write_count;      /* incremented in write() */
    u32     error_count;      /* incremented on copy_to/from_user failures */
    bool    debug_verbose;    /* controlled from debugfs */
    u32     reg_status;       /* fake register - updated on every write() */
    /* ... */
};
```
`container_of()` is used in `mydrv_open()` to get the private data back
from the `inode->i_cdev` pointer:

```c
struct mydrv_dev *dev = container_of(inode->i_cdev, struct mydrv_dev, c_dev);
file->private_data = dev;
```

After `open()` stores it in `file->private_data`, all other callbacks
(`read`, `write`, `release`) retrieve it directly from there.


#### Section 2 - `Sysfs` attributes

`name` and `value` are grouped with `sysfs_create_group()` and appear
under `/sys/class/mydrv/mydrv0/`. Both use `dev_get_drvdata()` to reach
the private struct - the `device_create()` call stored it there via the
`drvdata` parameter.

#### Section 3 - `Debugfs`

Variable helpers - live pointers

```c
debugfs_create_u32 ("open_count",    0444, dev->dbg_dir, &dev->open_count);
debugfs_create_u32 ("read_count",    0444, dev->dbg_dir, &dev->read_count);
debugfs_create_u32 ("write_count",   0444, dev->dbg_dir, &dev->write_count);
debugfs_create_u32 ("error_count",   0444, dev->dbg_dir, &dev->error_count);
debugfs_create_bool("debug_verbose", 0644, dev->dbg_dir, &dev->debug_verbose);
debugfs_create_x32 ("reg_status",    0444, dev->dbg_dir, &dev->reg_status);
```

These are live - every time we `cat` the file, read the current value
of that variable inside the running driver. No caching, no snapshot.

stats - `seq_file` multi-line output

```c
static int mydrv_stats_show(struct seq_file *s, void *unused)
{
    struct mydrv_dev *dev = s->private;
    /* dev was passed as data= to debugfs_create_file(), stored in i_private,
       then passed to single_open() which puts it in s->private */

    seq_printf(s, "open_count:    %u\n",  dev->open_count);
    seq_printf(s, "write_count:   %u\n",  dev->write_count);
    seq_printf(s, "reg_status:    0x%08X\n", dev->reg_status);
    /* ... */
    return 0;
}
```
`single_open()` + `seq_read` + `single_release` is the standard pattern
for a read-only `debugfs` file with formatted output.
reset - write-only counter reset
Writing anything to `/sys/kernel/debug/mydrv/reset` zeroes all counters.
Uses simple_open which copies `inode->i_private` into `file->private_data`
automatically.

#### Section 4 - Probe / remove

**Probe** - Creation order
```
alloc_chrdev_region()    ← device number first
cdev_init() + cdev_add() ← register with kernel
class_create()           ← class for udev
device_create()          ← /dev node + /sys/class/mydrv/mydrv0/
sysfs_create_group()     ← attributes under class device
debugfs_create_dir()     ← /sys/kernel/debug/mydrv/
debugfs_create_*()       ← all entries under the dir
```

**Remove** - strict reverse order
```
debugfs_remove()         ← first - stops reads of live pointers
sysfs_remove_group()     ← then sysfs attributes
device_destroy()         ← then class device
class_destroy()          ← then class
cdev_del()               ← then cdev
unregister_chrdev_region ← last
```

`debugfs_remove()` is called first in `remove()` - before anything else.
This is important. If `remove()` frees the private data or destroys the
device while a debugfs file is still open and pointing at live variables,
the result is a kernel crash. Removing debugfs first closes that window.     

**Error path in probe**

If any step fails, the error path undoes everything that already succeeded
in reverse order before returning the error code.

---------

### Testing

* Build
    ```bash
    make
    ```

* Running the Example  

    Open three terminals:    
    * Terminal 1: `insmod` / `rmmod` and test commands     
    * Terminal 2: `dmesg -w`     
    * Terminal 3: `watch -n1 cat /sys/kernel/debug/mydrv/stats`  (after loaded)

* Load the module
    ```bash
    sudo insmod mydrv.ko
    ```

* Verify all three interfaces
    ```bash
    # switch to supper user
    sudo su

    # char device
    ls -l /dev/mydrv0

    # sysfs
    ls /sys/class/mydrv/mydrv0/
    cat /sys/class/mydrv/mydrv0/name
    cat /sys/class/mydrv/mydrv0/value

    # debugfs
    ls /sys/kernel/debug/mydrv/
    cat /sys/kernel/debug/mydrv/stats
    ```

* Test the character device
    ```bash
    # Initial read - returns the default kbuf content
    cat /dev/mydrv0
    # hello from mydrv

    # Write new content
    echo "hello debugfs" > /dev/mydrv0

    # Read it back
    cat /dev/mydrv0
    # hello debugfs

    # Check the counters updated in debugfs
    cat /sys/kernel/debug/mydrv/open_count
    cat /sys/kernel/debug/mydrv/write_count
    cat /sys/kernel/debug/mydrv/read_count

    # reg_status is updated with the byte count of the last write
    cat /sys/kernel/debug/mydrv/reg_status
    ```

* Enable verbose logging
    ```bash
    # Turn on verbose kernel log output
    echo Y > /sys/kernel/debug/mydrv/debug_verbose

    # Now every open/read/write prints to dmesg
    cat /dev/mydrv0
    echo "test" > /dev/mydrv0

    # Watch dmesg - we will see open/read/write log lines

    # Turn it off again
    echo N > /sys/kernel/debug/mydrv/debug_verbose
    ```

* Test sysfs value attribute
    ```bash
    # Read current value
    cat /sys/class/mydrv/mydrv0/value
    # 0

    # Write a new value
    echo "42" > /sys/class/mydrv/mydrv0/value

    # Read it back
    cat /sys/class/mydrv/mydrv0/value
    # 42
    ```

* Reset all counters
    ```bash
    # Write anything to reset - the content doesn't matter
    echo 1 > /sys/kernel/debug/mydrv/reset
    ```
* Unload
    ```bash
    sudo rmmod mydrv
    ```
    All three interfaces disappear cleanly - `/dev/mydrv0`, `/sys/class/mydrv/`,
    and `/sys/kernel/debug/mydrv/` are all gone.

### The Full Interface Map
```yml
/dev/
  └── mydrv0                         ← char device - read/write kbuf

/sys/class/mydrv/mydrv0/
  ├── name                           ← sysfs RO - driver name string
  └── value                          ← sysfs RW - integer, cat/echo

/sys/kernel/debug/mydrv/
  ├── open_count                     ← debugfs u32 RO  - live counter
  ├── read_count                     ← debugfs u32 RO  - live counter
  ├── write_count                    ← debugfs u32 RO  - live counter
  ├── error_count                    ← debugfs u32 RO  - live counter
  ├── debug_verbose                  ← debugfs bool RW - Y/N
  ├── reg_status                     ← debugfs x32 RO  - hex, updated on write()
  ├── stats                          ← debugfs custom  - full formatted dump
  └── reset                          ← debugfs write   - zeroes all counters
```