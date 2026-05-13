
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

------------------
3. remove order
```bash
sudo rmmod mydriver
sudo rmmod mydevice
sudo rmmod mybus
```

```
                +------------------+
                |    mybus.ko      |
                |   (bus_type)     |
                +------------------+
                         |
          --------------------------------
          |                              |
          v                              v
+------------------+          +------------------+
|   mydevice.ko    |          |   mydriver.ko    |
| registers device |          | registers driver |
|    mydev0        |          |  name = mydev0   |
+------------------+          +------------------+
          |                              |
          ------------ match() ----------
                         |
                         v
                       probe()
```

-----------------------------------------

## Check steps:
### Step 1: Load the Bus  
```bash
sudo insmod mybus.ko
```
Kernel log:
```
mybus: init
mybus: registered
```
This means:

1. `bus_register()` created `/sys/bus/mybus`
2. `device_register()` created `/sys/devices/mybus0`

#### Check Bus Registration
```bash
ls /sys/bus/mybus/
```
Output:
```
devices  drivers  drivers_autoprobe  drivers_probe  uevent  version
```
| Entry             | Description                    |
| ----------------- | ------------------------------ |
| devices           | devices attached to this bus   |
| drivers           | drivers registered to this bus |
| drivers_autoprobe | automatic probing control      |
| drivers_probe     | manual probe trigger           |
| uevent            | udev event interface           |
| version           | custom sysfs attribute         |


#### Check Bus Attribute

```bash
cat /sys/bus/mybus/version 
```
output:
```
1.0
```
This is a custom bus attribute exported through sysfs.

#### Check Devices
```
ls /sys/bus/mybus/devices/
```
No output yet.   
Reason :  **no devices registered yet**  


#### Check Drivers

```
ls /sys/bus/mybus/drivers/
```
No output yet.   
Reason: **no drivers registered yet**

#### Bus Device
```
ls /sys/devices/mybus0/
```
Output:
```
power uevent
```
Explanation: mybus0 is the parent device created by:
```
static struct device my_bus_device
```
>> All devices on this custom bus become children of this parent device.

#### udev Events After Bus Registration
```
KERNEL add /bus/mybus
KERNEL add /module/mybus

UDEV add /bus/mybus
UDEV add /module/mybus
```
| Event             | Meaning              |
| ----------------- | -------------------- |
| add /bus/mybus    | new bus registered   |
| add /module/mybus | kernel module loaded |


### Step 2: Load the Device

```bash
sudo insmod mydevice.ko
```
Kernel log:
```
mydevice: init
mydevice: device registered
```
This registers:  
```
mydev0
```
onto:
```
mybus0
```

#### Check Device Registration
```bash
ls /sys/bus/mybus/devices/
```
Output:
```
mydev0
```
The device is now registered to the bus.

#### Check Device Attribute
```bash
cat /sys/bus/mybus/devices/mydev0/type
```
Output:
```
type : mydev0
```
This attribute comes from:
```c
DEVICE_ATTR(type, 0444, my_show_type, NULL);
```

#### Check Device Location
```bash
ls /sys/devices/mybus0/
```
Output:
```
mydev0 power uevent
```
mydev0 becomes a child device under:
```
/sys/devices/mybus0/
```
because:
```c
mydev->dev.parent = &my_bus_device;
```
#### Check Drivers
```
ls /sys/bus/mybus/drivers/
```
Still empty.     
Reason:
* no driver loaded yet
* device exists without a driver     

This is a very important Linux concept:

>>devices can exist before drivers

The kernel keeps the device registered and waits for a matching driver.

#### udev Events After Device Registration
```
KERNEL add /devices/mybus0/mydev0
UDEV add /devices/mybus0/mydev0
```
| Event                      | Meaning                 |
| -------------------------- | ----------------------- |
| add /devices/mybus0/mydev0 | device created in sysfs |

### Step 3 : Load the Driver
```bash
sudo insmod mydriver.ko
```
Kernel log:
```
mydriver: init
mybus: match called
mydriver: probe called for mydev0
```

#### What Happened Internally

When the driver registers:
```c
driver_register()
```
the kernel automatically:

1. scans devices on the bus
2. calls the bus match() function
3. finds matching device
4. binds driver to device
5. calls probe()

#### Binding Sequence
```
  driver_register()
        ↓
    bus match()
        ↓
    match success
        ↓
      probe()
```

#### Verify Driver Registration
```bash
ls /sys/bus/mybus/drivers/
```
Output:
```
mydev0
```
The driver is now attached to the device.

-------------------------

**Notice this log order:**   
```
mybus: match called
mydriver: probe called for mydev0
```
This proves:
```
probe() is NOT called directly by the driver
```
Instead:
```
the Linux driver core calls probe()
only after a successful match.
```
This is one of the most important Linux Device Model concepts.

-----

#### udev Events During Binding
```
KERNEL bind /devices/mybus0/mydev0
UDEV bind /devices/mybus0/mydev0
```
The driver and device are now connected.

----------------------------------------

### Module Removal Order
````bash
sudo rmmod mydriver
sudo rmmod mydevice
sudo rmmod mybus
````
Correct removal order:
```
driver → device → bus
```
Reason:

* driver depends on device/bus
* device depends on bus

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

