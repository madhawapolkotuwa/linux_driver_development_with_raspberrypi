# Platform Driver vs Character Device Driver Registration

These are two different layers of the Linux driver model that often work together.

-----

## Character Device Driver Registration

Registers a driver that exposes a file interface (`/dev/mydevice`) to userspace. *It's about how userspace accesses the device.*

```c
// 1. Allocate a major number
alloc_chrdev_region(&dev_num, 0, 1, "mydev");

// 2. Initialize and add the cdev
cdev_init(&my_cdev, &my_fops);   // link file_operations
cdev_add(&my_cdev, dev_num, 1);

// 3. Expose file operations to userspace
static struct file_operations my_fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .read    = my_read,
    .write   = my_write,
    .release = my_release,
};
```

>> What it answers: "How does userspace talk to this device?" (via `read/write/ioctl syscalls`)

-----

## Platform Driver Registration

Registers a driver with the kernel's **device/driver bus model**. It's about binding a driver to hardware described in **Device Tree** or **ACPI**.

```c
// 1. Define match table (Device Tree compatible strings)
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,mydevice" },
    { }
};

// 2. Define platform driver
static struct platform_driver my_platform_driver = {
    .probe  = my_probe,    // called when hardware is found
    .remove = my_remove,   // called on unbind/removal
    .driver = {
        .name           = "mydevice",
        .of_match_table = my_of_match,
    },
};

// 3. Register
platform_driver_register(&my_platform_driver);
// or the shorthand macro:
module_platform_driver(my_platform_driver);
```

>> What it answers: "Which hardware does this driver own, and how does it initialize it"

------

### The `probe()` Function - The Key Concept

`probe()` is called by the kernel automatically when it matches a device to our driver.     
This is where we typically also set up our character device:

```c
static int my_probe(struct platform_device *pdev)
{
    // 1. Get hardware resources (IRQ, memory, clocks...)
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    base = devm_ioremap_resource(&pdev->dev, res);

    // 2. NOW register the char device so userspace can use it
    alloc_chrdev_region(&dev_num, 0, 1, "mydev");
    cdev_init(&my_cdev, &my_fops);
    cdev_add(&my_cdev, dev_num, 1);

    return 0;
}
```

--------

### Side-by-Side Comparison

Aspect | Platform Driver | Character Device
-------|-----------------|------------------
Purpose | Bind driver to hardware | Expose `/dev` interface to userspace
Key struct | platform_driver | `cdev + file_operations`
Registration fn | platform_driver_register() | cdev_add()
Triggered by | Kernel device/DT matching | Userspace `open("/dev/x")`
Callbacks | `probe()`, `remove()` | `open()`, `read()`, `write()`, `ioctl()`
Layer | Kernel bus model | VFS / syscall interface
Optional? | Yes (PCI, SPI, I2C etc. are alternatives) | Yes (netdev, input subsystem are alternatives)

```
Device Tree / ACPI
      │
      ▼
Platform Bus matches device + driver
      │
      ▼
 probe() called  ◄── Platform Driver layer
      │
      │  (hardware init, resource mapping)
      │
      ▼
 cdev_add()      ◄── Character Device layer
      │
      ▼
 /dev/mydevice   ◄── Userspace access via VFS
```

* **In short**: the platform driver is the hardware binding layer, and the character device is the userspace interface layer. Most real drivers implement both - `probe()` sets up hardware AND registers the char device.

---------------------


## Examples :- ( I2C, SPI, etc ) 

## Two Views of the Same Device
The device appears in two places because the Linux device model has two distinct systems that serve different purposes.

-----

### `/dev/i2c-1`, `/dev/spidev0.0` → The Userspace Interface

This is the **character device node** created so that applications can use the bus.

```
/dev/i2c-1        → major:minor number → cdev → file_operations
/dev/spidev0.0
```

* Created by the `i2c-dev` / `spidev` userspace-facing driver
* Lets you do `open()`, `read()`, `write()`, `ioctl()` from Python, C, etc.
* It is not the hardware itself - it's a doorway to it
* Think of it as: "**how do I talk to this bus?**"

```python
# Using /dev - pure userspace I/O
bus = smbus2.SMBus(1)        # opens /dev/i2c-1
bus.write_byte(0x48, 0x00)
```

----------------

### `/sys/bus/i2c/`, `/sys/bus/spi/` → The Kernel Device Model

This is the **sysfs representation** of the kernel's internal `bus/device/driver` tree.
```
/sys/bus/i2c/
├── devices/
│   ├── i2c-1/          → the bus adapter itself
│   └── 1-0048/         → a device at address 0x48 on bus 1
└── drivers/
    └── ds3231/         → driver bound to that device
```

* Managed by the platform/bus driver layer (`i2c_bus_type`, `spi_bus_type`)
* Reflects kernel-internal structures: `i2c_adapter`, `i2c_client`, `i2c_driver`
* Used for **introspection, binding/unbinding, power management**
* Think of it as: "**what devices exist and what drivers own them?**"

```bash
# Using /sys - kernel model inspection
cat /sys/bus/i2c/devices/1-0048/name      # what chip is this?
echo ds3231 0x68 > /sys/bus/i2c/devices/i2c-1/new_device  # instantiate device
```

-----

## The Full Picture
```
Hardware (BCM2835 I2C controller)
              │
              ▼
   ┌─────────────────────────────┐
   │   Platform Driver (probe)   │  ← /sys/bus/platform/drivers/
   │   maps registers, IRQs      │
   └────────────┬────────────────┘
                │ registers
                ▼
   ┌─────────────────────────────┐
   │   i2c_adapter (i2c core)    │  ← /sys/bus/i2c/devices/i2c-1/
   │   kernel bus abstraction    │
   └────────────┬────────────────┘
                │
       ┌────────┴────────┐
       ▼                 ▼
  i2c_client          i2c-dev driver
  (sensor)          (userspace bridge)
  /sys/bus/i2c/             │
  devices/1-0048/           ▼
                        cdev_add()
                            │
                            ▼
                        /dev/i2c-1
```


|    | `/dev/i2c-1/` | `sys/bus/i2c/`
|----|---------------|--------------
Purpose | Userspace I/O interface | Kernel device model tree
Who creates it | i2c-dev char driver | `i2c_bus_type` core
Who uses it | Applications, Python libs | Kernel, udev, sysadmins
What you can do | read/write/ioctl transactions | bind/unbind drivers, read attributes, power management
Reflects | A file operation interface | Kernel object hierarchy
Analogy | A socket you plug into | The wiring diagram behind the wall

>> `/dev` answers "how do I use this device?"  
>> `/sys` answers "what is this device and how is the kernel managing it?"

* They are two representations of the same underlying hardware, exposed through two different kernel subsystems : the `VFS/cdev` layer and the driver `model/sysfs` layer : that coexist by design in Linux.


----------------------
----------------------
<br>
<br>

## Creating a New Bus Driver (Like SPI) : Full Architecture
This is a deep topic. Creating a complete bus subsystem means implementing 3 layers:

```
New Bus Subsystem
├── 1. Bus Core (bus_type + infrastructure)
├── 2. Controller Driver (platform_driver + /dev access)
└── 3. Device Driver (our_bus_driver for devices on the bus)
```

### Layer 1 : Define the Bus Type (The "SPI Core" equivalent)

This is the kernel's internal representation of the bus.

```c
// mybus_core.c - the heart of the bus subsystem

#include <linux/bus.h>
#include <linux/device.h>
#include <linux/module.h>

/* ── Match function: does this driver support this device? ── */
static int mybus_match(struct device *dev, struct device_driver *drv)
{
    // Match by name (like platform bus), or by ID table (like SPI/I2C)
    return strcmp(dev->type->name, drv->name) == 0;
}

/* ── Uevent: what environment vars does udev get? ── */
static int mybus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "MODALIAS=mybus:%s", dev_name(dev));
    return 0;
}

/* ── The bus_type - the central kernel object ── */
struct bus_type mybus_bus_type = {
    .name    = "mybus",
    .match   = mybus_match,
    .uevent  = mybus_uevent,
};
EXPORT_SYMBOL(mybus_bus_type);

/* ── Device type on the bus ── */
static void mybus_dev_release(struct device *dev)
{
    kfree(dev); // cleanup when refcount hits 0
}

struct device_type mybus_device_type = {
    .name    = "mybus_device",
    .release = mybus_dev_release,
};
EXPORT_SYMBOL(mybus_device_type);

/* ── Core structs (like spi_device, spi_driver) ── */

struct mybus_device {
    struct device   dev;         // MUST be first
    u32             bus_freq;    // bus-specific config
    u8              chip_select;
    void            *controller_data;
};

struct mybus_driver {
    struct device_driver    driver;  // MUST be first
    int (*probe) (struct mybus_device *);
    void (*remove)(struct mybus_device *);
    const struct mybus_device_id *id_table;
};

/* ── Register/unregister helpers (like spi_register_driver) ── */
int mybus_register_driver(struct mybus_driver *drv)
{
    drv->driver.bus = &mybus_bus_type;
    return driver_register(&drv->driver);
}
EXPORT_SYMBOL(mybus_register_driver);

void mybus_unregister_driver(struct mybus_driver *drv)
{
    driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(mybus_unregister_driver);

/* ── Add/remove devices on the bus ── */
int mybus_add_device(struct mybus_device *mbdev)
{
    mbdev->dev.bus  = &mybus_bus_type;
    mbdev->dev.type = &mybus_device_type;
    return device_add(&mbdev->dev);
}
EXPORT_SYMBOL(mybus_add_device);

static int __init mybus_core_init(void)
{
    return bus_register(&mybus_bus_type);
}
postcore_initcall(mybus_core_init);  // early init, before most drivers
```

>> After this, `/sys/bus/mybus/` appears automatically.

------------

## Layer 2 : Controller Driver (Hardware + /dev access)
This is the platform driver that owns the hardware AND creates `/dev/mybus0`.

```c
// mybus_controller.c

#define MYBUS_MAJOR     0       // 0 = dynamic allocation
#define MAX_CONTROLLERS 4

struct mybus_controller {
    struct platform_device  *pdev;
    void __iomem            *base;      // mapped registers
    int                     irq;
    struct cdev             cdev;       // /dev interface
    dev_t                   devt;
    struct device           *dev;
    struct mutex            bus_lock;   // serialize transfers

    // The hardware register offsets
    // Transfer queue, DMA buffers, etc.
};

/* *****************************************
   /dev FILE OPERATIONS  (userspace access)
   ***************************************** */

static int mybus_open(struct inode *inode, struct file *filp)
{
    struct mybus_controller *ctrl;
    ctrl = container_of(inode->i_cdev, struct mybus_controller, cdev);
    filp->private_data = ctrl;
    return 0;
}

/* Custom ioctl commands (like SPI_IOC_MESSAGE) */
#define MYBUS_IOC_MAGIC     'M'
#define MYBUS_IOC_TRANSFER  _IOWR(MYBUS_IOC_MAGIC, 1, struct mybus_transfer)
#define MYBUS_IOC_SET_FREQ  _IOW (MYBUS_IOC_MAGIC, 2, uint32_t)

struct mybus_transfer {
    __u8  *tx_buf;
    __u8  *rx_buf;
    __u32  len;
    __u32  speed_hz;
};

static long mybus_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct mybus_controller *ctrl = filp->private_data;
    struct mybus_transfer xfer;

    switch (cmd) {
    case MYBUS_IOC_TRANSFER:
        if (copy_from_user(&xfer, (void __user *)arg, sizeof(xfer)))
            return -EFAULT;
        mutex_lock(&ctrl->bus_lock);
        // ← actual hardware transfer here
        mybus_hw_transfer(ctrl, &xfer);
        mutex_unlock(&ctrl->bus_lock);
        return 0;

    case MYBUS_IOC_SET_FREQ:
        // configure hardware clock divider
        return 0;

    default:
        return -ENOTTY;
    }
}

static ssize_t mybus_read(struct file *filp, char __user *buf,
                           size_t count, loff_t *f_pos)
{
    struct mybus_controller *ctrl = filp->private_data;
    u8 rx_buf[256];

    mutex_lock(&ctrl->bus_lock);
    mybus_hw_read(ctrl, rx_buf, count);
    mutex_unlock(&ctrl->bus_lock);

    return copy_to_user(buf, rx_buf, count) ? -EFAULT : count;
}

static ssize_t mybus_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *f_pos)
{
    struct mybus_controller *ctrl = filp->private_data;
    u8 tx_buf[256];

    if (copy_from_user(tx_buf, buf, count))
        return -EFAULT;

    mutex_lock(&ctrl->bus_lock);
    mybus_hw_write(ctrl, tx_buf, count);
    mutex_unlock(&ctrl->bus_lock);

    return count;
}

static const struct file_operations mybus_fops = {
    .owner          = THIS_MODULE,
    .open           = mybus_open,
    .read           = mybus_read,
    .write          = mybus_write,
    .unlocked_ioctl = mybus_ioctl,
};

/* **************************************
   PLATFORM DRIVER  (hardware binding)
   ************************************** */

static struct class *mybus_class;  // for /dev node auto-creation

static int mybus_probe(struct platform_device *pdev)
{
    struct mybus_controller *ctrl;
    struct resource *res;
    int ret;

    ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
    if (!ctrl) return -ENOMEM;

    ctrl->pdev = pdev;
    mutex_init(&ctrl->bus_lock);

    /* 1. Map hardware registers */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    ctrl->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(ctrl->base)) return PTR_ERR(ctrl->base);

    /* 2. Get IRQ */
    ctrl->irq = platform_get_irq(pdev, 0);
    ret = devm_request_irq(&pdev->dev, ctrl->irq,
                            mybus_irq_handler, 0, "mybus", ctrl);
    if (ret) return ret;

    /* 3. Initialize hardware */
    mybus_hw_init(ctrl);

    /* 4. Allocate char device number */
    ret = alloc_chrdev_region(&ctrl->devt, 0, 1, "mybus");
    if (ret) return ret;

    /* 5. Register cdev → links to file_operations */
    cdev_init(&ctrl->cdev, &mybus_fops);
    ctrl->cdev.owner = THIS_MODULE;
    ret = cdev_add(&ctrl->cdev, ctrl->devt, 1);
    if (ret) goto err_cdev;

    /* 6. Create /dev/mybus0 automatically via udev */
    ctrl->dev = device_create(mybus_class, &pdev->dev,
                              ctrl->devt, ctrl, "mybus%d",
                              pdev->id);
    if (IS_ERR(ctrl->dev)) { ret = PTR_ERR(ctrl->dev); goto err_dev; }

    platform_set_drvdata(pdev, ctrl);

    dev_info(&pdev->dev, "mybus controller registered as /dev/mybus%d\n",
             pdev->id);
    return 0;

err_dev:
    cdev_del(&ctrl->cdev);
err_cdev:
    unregister_chrdev_region(ctrl->devt, 1);
    return ret;
}

static int mybus_remove(struct platform_device *pdev)
{
    struct mybus_controller *ctrl = platform_get_drvdata(pdev);
    device_destroy(mybus_class, ctrl->devt);
    cdev_del(&ctrl->cdev);
    unregister_chrdev_region(ctrl->devt, 1);
    mybus_hw_disable(ctrl);
    return 0;
}

static const struct of_device_id mybus_of_match[] = {
    { .compatible = "vendor,mybus-controller" },
    { }
};
MODULE_DEVICE_TABLE(of, mybus_of_match);

static struct platform_driver mybus_platform_driver = {
    .probe  = mybus_probe,
    .remove = mybus_remove,
    .driver = {
        .name           = "mybus",
        .of_match_table = mybus_of_match,
    },
};

static int __init mybus_init(void)
{
    mybus_class = class_create(THIS_MODULE, "mybus");
    if (IS_ERR(mybus_class)) return PTR_ERR(mybus_class);
    return platform_driver_register(&mybus_platform_driver);
}
module_init(mybus_init);
```

-----------------------

## Layer 3 : Device Driver (A driver for chips on the bus)
```c
// mybus_sensor.c - a driver for a device connected to the bus

static int mysensor_probe(struct mybus_device *mbdev)
{
    dev_info(&mbdev->dev, "mysensor found on mybus\n");
    // initialize the sensor chip via mybus transactions
    mybus_transfer(...);
    return 0;
}

static const struct mybus_device_id mysensor_ids[] = {
    { "mysensor", 0 },
    { }
};

static struct mybus_driver mysensor_driver = {
    .driver   = { .name = "mysensor" },
    .probe    = mysensor_probe,
    .id_table = mysensor_ids,
};
module_driver(mysensor_driver,
              mybus_register_driver,
              mybus_unregister_driver);
```

------------------------

```
Device Tree
  vendor,mybus-controller
          │
          │ platform_driver match
          ▼ 
    mybus_probe()
      │
      ├─── ioremap()          hardware registers
      ├─── request_irq()      interrupt handler
      ├─── mybus_hw_init()    clock/reset setup
      │
      ├─── cdev_add()    ──────────────► /dev/mybus0
      │                                  open/read/write/ioctl
      │                                  (direct userspace access)
      │
      └─── bus_register() ─────────────► /sys/bus/mybus/
                │                         devices/
                │                         drivers/
                ▼
         mybus_match()
                │
                ▼
         mysensor_probe()   ──────────► /sys/bus/mybus/devices/mybus-mysensor
```

What | How
-----|-----
`/sys/bus/mybus/` | `appearsbus_register(&mybus_bus_type)`
/dev/mybus0 |  `appearscdev_add()` + `device_create()` in `probe()`
Driver auto-binds to device | `mybus_match()` + `mybus_register_driver()`
Userspace does transactions | `ioctl(fd, MYBUS_IOC_TRANSFER, &xfer)`
Kernel drivers do transaction | `smybus_transfer()` internal API

>> `/dev` and `/sys` are created separately -> `/sys` comes from `bus_register()`,    
>> `/dev` comes from `cdev_add()` -> but both happen inside `probe()` when the hardware is found.







