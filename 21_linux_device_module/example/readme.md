
# Example

1. build
```bash
make
```
2. load order

>> monitor the udev output using `udevadm monitor`

```bash
sudo insmod mybus.ko
sudo insmod mydevice.ko
sudo insmod mydriver.ko
```

Expected Output `dmesg`:
```
mybus: init
mybus: registered

mydevice: init
mydevice: device registered

mydriver: init
mydriver: probe called for mydev0
```
------------------
3. remove order
```bash
sudo rmmod mydriver
sudo rmmod mydevice
sudo rmmod mybus
```

Expected Output:
```
mydriver: remove called for mydev0
mydriver: exit

mydevice: exit

mybus: exit
```


-----------------------------------------
<br>
<br>

# Example `sysfs` Demonstration

```
kobject
   ↓
  bus
   ↓
 device
   ↓
 driver
   ↓
probe/remove
   ↓
 sysfs
```
This example implements exactly that architecture.

------------

## Step 1 : `mybus.c`

**Creating the Bus**

>> A bus is the communication channel between `devices` and the `processor`.

In this example:
```c
struct bus_type my_bus_type = {
    .name  = "mybus",
    .match = my_match,
};
```
This creates a custom Linux bus.

The bus core internally creates:
```
/sys/bus/mybus/
```
when this runs:
```c
bus_register(&my_bus_type);
```

#### Why `extern struct bus_type my_bus_type;`:

In `mybus.h` we declare:
```c
extern struct bus_type my_bus_type;
```
because:
* the real object is defined in `mybus.c`
* other files need access to it

Especially:
* `mydevice.c`
* `mydriver.c`

need to attach themselves to this bus.

This is standard Linux kernel modular design.

-----------------

## Step 2 : The Bus `.match()` Function
>> `.match()` is called whenever a new device or driver appears on the bus.

Here:  
```c
static int my_match(struct device *dev,
                    struct device_driver *drv)
{
    return !strcmp(dev_name(dev), drv->name);
}
```

This means:  
```
If device name == driver name
→ match successful
→ call probe()
```
------------------

## Step 3 : Parent Bus Device
This part is extremely important conceptually.
```c
static struct device my_bus_device = {
    .init_name = "mybus0",
    .release   = my_bus_release,
};
```
>> Every device exists in a hierarchy.

The bus itself is represented as a parent device.

This becomes:
```
/sys/devices/mybus0/
```
Then child devices appear under it.

This is how Linux builds the device tree hierarchy internally.

------------------------

## Step 4 : `struct device`
>> `struct device` is the core representation of a device.

here:
```c
struct my_device {
    char *name;
    struct device dev;
};
```

This demonstrates the most important Linux pattern:
```
embed generic kernel object
inside subsystem-specific object
```
-------------------

## Why Embed `struct device`
Linux uses composition instead of inheritance.

By embedding:
```c
struct device dev;
```

our object automatically gains:

* sysfs support
* reference counting
* uevents
* parent hierarchy
* driver binding support

because `struct device` already contains:

* kobject
* bus pointer
* driver pointer
* release callback

--------------

## Step 5 : Registering the Device

In `mydevice.c` :
```c
my_register_device(&mydev);
```
we eventually call:
```c
device_register(&mydev->dev);
```
Before registering:
```c
mydev->dev.bus = &my_bus_type;
```
This attaches the device to our bus.

And:   
```c
mydev->dev.parent = &my_bus_device;
```
creates the hierarchy:
```
mybus0
   └── mydev0
```
visible in:
```
/sys/devices/
```
---------------------

## Step 6 : `dev_set_name()`

in `mybus.c`:
```c
dev_set_name(&mydev->dev, mydev->name);
```
sets the `sysfs` directory name.

So Linux creates:
```
/sys/devices/mybus0/mydev0/
```
This directly comes from the embedded `kobject`.

--------------------------

## Step 7 : Driver Registration
In `mydriver.c`:
```c
static struct my_driver mydrv = {
    .driver = {
        .name = "mydev0",
    },
};
```
Again:

* embed generic object
* extend with bus-specific wrapper

```c
struct device_driver
```
this is the generic driver representation.

--------------

## Step 8 : Driver Attached to Bus

When this runs:
```c
drv->driver.bus = &my_bus_type;
```
the driver becomes part of:
```
/sys/bus/mybus/drivers/
```
through call:
```c
driver_register()
```
----------------

## Step 9 : Automatic Matching
Now the important Linux Device Model moment happens.   
Kernel internally does:  
```
new driver registered
        ↓
scan all devices on this bus
        ↓
call my_match()
        ↓
match successful
        ↓
call probe()
```
This is the real Linux driver lifecycle.

Not:
```
module_init() directly controls hardware
```
but:
```
kernel binding engine controls driver lifecycle
```
---------------

## Step 10 : `probe()`

This runs automatically:   
```c
static int my_probe(struct device *dev)
```
because:

* device exists
* driver exists
* match succeeded

>> The kernel found our hardware and matched it with this driver.

That is exactly what happened here.

------------------

## Step 11 : Why `struct device *`

The kernel passes:
```c
struct device *dev
```
because the Linux device model is generic.   
The bus core does not know:
* our custom structs
* our hardware
* our driver internals

It only understands:   
```
generic device objects
```

## Step 12 : Where `container_of()` Fits
Suppose we do:
```c
struct my_device *mydev;

mydev = container_of(dev, struct my_device, dev);
```
This walks backward from:
```
embedded struct device
```
to:  
```
outer struct my_device
```
This is one of the core Linux kernel programming patterns.

-----------------------

## Step 13 : `sysfs` Hierarchy
After everything loads:  
```
/sys/
├── bus/
│   └── mybus/
│       ├── devices/
│       │   └── mydev0
│       └── drivers/
│           └── mydev0
│
└── devices/
    └── mybus0/
        └── mydev0/
```
This directly visualizes:

* bus
* device
* driver
* hierarchy
* binding

---------------------------

## Step 14 : `remove()`

when:
```bash
sudo rmmod mydriver
```
happens:

Kernel does:
```
unbind driver
    ↓
call remove()
    ↓
detach from device
```
>> The kernel is detaching from hardware. Leave cleanly.

-------------------------------
-------------------------------
-------------------------------

<br>

## The Most Important Lesson

This example teaches the core Linux truth:
```
Drivers do not own devices.
The kernel owns devices.
Drivers attach to them dynamically.
```
>> That is the real Linux Device Model philosophy.



----------------------
----------------------
----------------------
<br>

## Extra knowledge 

In this tutorial, matching is done manually by comparing:
```
device name == driver name
```
inside our custom `.match()` function.

```c
static int my_match(struct device *dev,
                    struct device_driver *drv)
{
    return !strcmp(dev_name(dev), drv->name);
}
```

>> But in real Linux systems, matching is usually performed automatically
>> using `identifiers` provided by the bus subsystem.

For embedded systems, the most common mechanism is the `Device Tree` compatible string.

### Example : SPI driver
Watch the full Linux SPI explanation video [here](../../16_spi_subsytem_basics/)
```c
static const struct of_device_id my_spi_of_match[] = {
    { .compatible = "vendor,my-spi-device" },
    { }
};
MODULE_DEVICE_TABLE(of, my_spi_of_match);

static struct spi_driver my_spi_driver = {
    .driver = {
        .name           = "my_spi_driver",
        .of_match_table = my_spi_of_match,
    },
};
```
Device Tree node:  
```json
spi_device@0 {
    compatible = "vendor,my-spi-device";
};
```

The SPI bus core compares:   
```
DT compatible string  ⇔  driver's of_match_table
```
If matched:  
```
SPI bus ➡ calls probe()
```

### Example : Same concept for I2C:

Watch the full Linux I2C explanation video [here](../../14_i2c_subsystem_basics/)

```c
static const struct of_device_id my_i2c_of_match[] = {
    { .compatible = "vendor,my-i2c-device" },
    { }
};

static struct i2c_driver my_i2c_driver = {
    .driver = {
        .name           = "my_i2c_driver",
        .of_match_table = my_i2c_of_match,
    },
};
```
--------

real subsystems like:
* SPI
* I2C
* Platform bus
* PCI
* USB

usually provide their own matching mechanisms internally.

*So our custom bus example simplifies the matching process to help explain*

