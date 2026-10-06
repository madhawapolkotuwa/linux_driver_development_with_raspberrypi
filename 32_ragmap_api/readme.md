# Regmap API: Simplifying Register Access for I2C/SPI Devices

## Video :-

[![Youtube Video](https://img.youtube.com/vi/YkmLAkPQypU/0.jpg)](https://www.youtube.com/watch?v=YkmLAkPQypU)

Every driver we've built so far in this series has talked to hardware in a fairly manual way,
mapping memory with ioremap, or issuing raw **I2C/SPI** transfers by hand.   
The Regmap API (`regmap_*` functions, defined in `linux/regmap.h`) is the kernel's answer to that repetition.   
It gives you a single, bus-agnostic interface for reading and writing device registers, regardless of whether the underlying transport is **I2C**, **SPI**, **MMIO**, or even **SoundWire**.

## Why Regmap?
Many hardware devices contain a collection of registers that are used to:

* Configure the device
* Read device status
* Read sensor measurements
* Enable or disable features
* Configure interrupts
* Control power modes
* Read identification information

For example, MPU-6500 IMU 6-axis sensor contains registers such as:
Register | Description
---------|------------
0x43 | Gyroscope X High Byte
0x44 | Gyroscope X Low Byte
0x45 | Gyroscope Y High Byte
0x46 | Gyroscope Y Low Byte
0x47 | Gyroscope Z High Byte
0x48 | Gyroscope ZLow Byte
0x6B | Power Management 1
0x75 | WHO_AM_I

A Linux driver needs a way to access these registers.

Regmap solves all four problems in one abstraction layer

Regmap stands for: `Register Map`    
* It provides a common abstraction for accessing device registers.
* Instead of writing bus-specific code every time we want to access a register, a driver can use:    
    `regmap_read();`   
    `regmap_write();`     
    `regmap_bulk_read();`     
    `regmap_bulk_write();`    
    `regmap_update_bits();`

* The Regmap subsystem handles the lower-level register access.

* `/lib/modules/$(uname -r)/kernel/drivers/base/regmap`
    ```bash
    ls /lib/modules/$(uname -r)/kernel/drivers/base/regmap
    regmap-i2c.ko.xz  regmap-spi.ko.xz
    ```

## The Basic Idea Behind Regmap

Without Regmap, a driver may need to know how to communicate with the hardware bus.

For example, an I²C driver may directly use:

`i2c_transfer();`      
`i2c_master_send();`  
`i2c_master_recv();`     
`i2c_smbus_read_byte();`     
`i2c_smbus_write_byte();`    
`i2c_smbus_read_i2c_block_data()`

The driver then needs to construct the appropriate I²C transactions.

Conceptually:
```
Device Driver 
    | 
    | 
Direct I2C API 
    | 
    v 
I2C Controller 
    | 
    v 
 Device
```

With Regmap:
```
Device Driver 
    | 
    | 
Regmap API 
    | 
    v 
Regmap Core 
    | 
    v 
Regmap Bus 
    | 
    v 
I2C Controller 
    | 
    v 
  Device
```

* The driver concentrates on **what register it wants to access**, while Regmap handles **how that register is transferred over the bus**.

## Why Was Regmap Created?
A large number of Linux devices have similar register-access requirements.

For example:
```
I2C Sensor
SPI Sensor
Audio Codec
PMIC
ADC
DAC
GPIO Expander
Display Controller
Clock Controller
Power Controller
```

* Many of these devices have a register-based interface.

* Without an abstraction layer, every driver could contain code like:  

    `i2c_transfer()` or `spi_trasfer()`  

* along with:
    * Register formatting
    * Value formatting
    * Endianness handling
    * Read/write operations
    * Bulk operations
    * Register locking
    * Register caching
    * Register access validation

    Regmap moves much of this common functionality into a reusable kernel subsystem.

### Different between `SMBus` & `Regmap`

Feature | SMBus(Hardware Protocol / Bus API) | Regmap(Linux Kernel Software Framework)
--------|-------|--------
Overview | A specific, strict subset of the **I2C physical communication protocol** that defines exact command sequences over physical wires. | An internal **software abstraction layer** inside the Linux kernel that simplifies how driver code interacts with hardware registers.
Operating Layer | Physical Bus / Low-Level Linux I2C Subsystem. | Linux Kernel Core (sys/kernel/debug/regmap/)
Hardware Portability | Locked to I2C/SMBus hardware lines. Switching to an SPI sensor requires rewriting the communication logic. | Bus Agnostic. The same driver code works seamlessly whether the chip is connected via I2C, SMBus, SPI, or MMIO.
Pros | **Highly Standardized**: Native support across almost all microcontrollers and SoC controllers.<br>**Low Overhead:** Consumes minimal RAM and CPU cycles.<br>**Fault Tolerant**: Emforces hardware timeouts to prevent slave device glitches from freezing the bus.<br>**Predictable Timing**: Every command triggers an immediate physical wire transaction. | **Hardware Portability**: Code remains untouched if the underlying physical bus changes.<br>**High Performance**: Features register caching to avoid wasting physical bus time on static reads.<br>**Thread-Safe**: Automatically manages mutex locks to handle multi-threaded environments seamlessly.<br>**Easy Bit Handling**: Provides functions like `regmap_update_bits()` for safe, atomic bit masking.
Cons | **Bus Dependent**: Deeply bound to specific hardware layouts.<br>**No Cache Optimization**: Duplicate reads physically clutter the bus, slowing down raw data streams.<br>**Manual Bit Masking**: Requires manual Read-Modify-Write cycles, risking data corruption across multiple threads. | **Kernel-Space Only**: Cannot be used directly in standard user-space applications (like Python or Bash scripts).<br>**Debugging Illusions**: Register caching can return values from RAM, masking physical chip resets or glitches.<br>**Higher Overhead**: Consumes more memory to maintain data structures, caches, and locks.


## Regmap Is Not a Bus Driver

An important concept is that Regmap does not replace **I²C** or **SPI**.     
**Regmap sits above the bus.**   
For Example:
```
MPU6500 Driver 
    | 
 regmap_read() 
    | 
    v 
 Regmap Core 
    | 
    v
 Regmap I2C 
    | 
    v 
 Linux I2C 
    | 
    v 
Raspberry Pi I2C 
    | 
    v 
 MPU6500
```

For same for SPI deviece:
```
Device Driver 
    | 
 regmap_read() 
    | 
    v 
 Regmap Core 
    | 
    v 
 Regmap SPI 
    | 
    v 
 Linux SPI 
    | 
    v 
 Device
```

>> The higher-level driver can therefore use the same style of register access regardless of whether the device is connected using I²C or SPI.

## What Problems Does Regmap Solve?

Regmap provides a number of common services for register-based devices.

### Register Read/Write

* Instead of implementing register transfers manually:

    * `regmap_read(map, reg, &value);`
    * `regmap_write(map, reg, value);`

### Bulk Register Access
* For consecutive registers:
    * `regmap_bulk_read(map, start_reg, buffer, count);`
* This is particularly useful for sensors.

    For example, the MPU6500 gyroscope contains:
    ```
    0x43 X High 
    0x44 X Low 
    0x45 Y High 
    0x46 Y Low 
    0x47 Z High 
    0x48 Z Low
    ```
    We can read all six bytes:
    ```c
    u8 data[6];

    regmap_bulk_read(map, 0x43, data, sizeof(data));
    ```

### Read-Modify-Write Operations
Many hardware registers contain multiple independent bit fields.
* For Example:  
    Register 0x1B (GYRO_CONFIG)

    ```
    +---+---+---+---+---+---+---+---+ 
    | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |  0x1B
    +---+---+---+---+---+---+---+---+
                  ^   ^
                  |   |
        overwrites ONLY bits 4 and 3
    ```
    We may want to change only bits 4 and 3 without changing the other bits.

    Regmap provides:
    ```c
    regmap_update_bits(map, reg, mask, value);
    // Example
    // This safely overwrites ONLY bits 4 and 3 with 'val', ignoring everything else
    ret = regmap_update_bits(regmap, 0x1B, 0x18, val); // 0x18 -> specify bits 4 and 3
    ```
    * This performs the appropriate read-modify-write operation.

## Regmap Register Configuration

Before using Regmap, the driver normally provides a:     
`struct regmap_config`   
This describes the register interface of the hardware.

For example:
```c
static const struct regmap_config mpu6500_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
};
```

This tells Regmap:

* Register address = 8 bits
* Register value   = 8 bits

For the MPU6500:
```
   Register
    8-bit
      |
      v
 +----------+ 
 |   0x75   | 
 +----------+
      |
      v
   Register
    Value
    8-bit
      |
      v
 +----------+ 
 |   0x70   | 
 +----------+
```
The current kernel `struct regmap_config` supports many additional properties, including register stride, register/value formatting, readable/writeable register callbacks, volatile-register handling, register ranges, bulk access limits, caching configuration, and more.

## Important Regmap Configuration Fields

### `reg_bits`
* Defines the number of bits used for the register address.
    * Example:   
    `.reg_bits = 8`, means **Register address = 8 bits**

### `val_bits`
* Defines the number of bits in a register value.
    * Example:   
    `.val_bits = 8`, means **each register contains an 8-bit value**.

### `reg_stride`
* Defines the spacing between valid registers.
    * Example:   
    `reg_stride = 1`, This allows:   
        ```
        0x00
        0x01
        0x02
        0x03
        ```
        If a device only has registers at every 4-byte boundary, a different stride can be specified `reg_stride = 4`.  This allows:
        ```
        0x00
        0x04
        0x08
        0x0C
        ```    
        >>If it is not specified, Regmap uses a default stride of 1.



### `max_register`
The driver can specify the highest valid register.   
For Example (MPU65000): `.max_register = 0x7E`   
This helps prevent accidental access outside the defined register map.

### `readable_reg()`
Some devices have registers that can be read and others that cannot.

A driver can provide:    
```c
static bool mpu6500_readable_reg(struct device *dev, unsigned int reg) 
{ 
    switch (reg) 
    { 
        case MPU6500_REG_WHO_AM_I: 
        case MPU6500_REG_PWR_MGMT_1: 
            return true; 
        default: 
            return false; 
    } 
}
```

Then:    
```c
static const struct regmap_config mpu6500_regmap_config = {
     .reg_bits = 8, 
     .val_bits = 8, 
     .readable_reg = mpu6500_readable_reg, 
};
```
This allows the driver to describe which registers are valid for reading.

### Writeable Registers
Similarly, a driver can describe which registers can be written.

```c
static bool mpu6500_writeable_reg(struct device *dev, unsigned int reg) { 
    switch (reg) 
    { 
        case MPU6500_REG_PWR_MGMT_1: 
            return true; 
        default: 
            return false; 
    } 
}
```

Then:
```c
static const struct regmap_config mpu6500_regmap_config = { 
    .reg_bits = 8, 
    .val_bits = 8, 
    .readable_reg = mpu6500_readable_reg, 
    .writeable_reg = mpu6500_writeable_reg, 
};
```

This is especially useful for devices where some registers are:  
* Read-only 
* Write-only 
* Read/Write 
* Reserved

### Volatile Registers
Some registers contain values that can change independently of the CPU.  

For example, sensor measurement registers:   
`GYRO_XOUT`, `GYRO_YOUT`, `GYRO_ZOUT`    
can change continuously as the sensor moves.     
These registers should generally be treated as **volatile**.     

A driver can provide:    
```c
static bool mpu6500_volatile_reg(struct device *dev, unsigned int reg) { 
    switch (reg) { 
        case MPU6500_REG_GYRO_XOUT_H: 
        case MPU6500_REG_GYRO_XOUT_L: 
        case MPU6500_REG_GYRO_YOUT_H: 
        case MPU6500_REG_GYRO_YOUT_L: 
        case MPU6500_REG_GYRO_ZOUT_H: 
        case MPU6500_REG_GYRO_ZOUT_L: 
            return true; 
        default: 
            return false; 
    } 
}
```

This becomes particularly important when Regmap caching is enabled.  

### Regmap Caching

One of the useful features of Regmap is register caching.    

Some hardware registers do not change unless the driver changes them.    

For example:     

* Power configuration 
* Clock configuration 
* Filter configuration 
* Interrupt configuration

Instead of always communicating with the hardware, Regmap can maintain a software cache.     

```
            Driver 
              | 
              v 
            Regmap 
           /      \ 
          /        \ 
         v          v 
    Register       Cache 
    Hardware
```

When appropriate, the driver can configure a cache.  

The kernel currently provides several cache types, including:    
Cache type|Behavior|Best for
----------|--------|--------
`REGCACHE_NONE`|No caching: every read/write hits the bus|Registers that change on their own (status/data registers)
`REGCACHE_FLAT`|Fixed-size array indexed by register address|Small, densely packed register maps
`REGCACHE_RBTREE`|Red-black tree of cached register blocks|Sparse register maps (most real-world chips)
`REGCACHE_MAPLE`|Maple tree: newer, generally faster than rbtree for large sparse maps|Large sparse maps on recent kernels

The kernel header notes that the Maple-tree cache is the preferred choice for new users in cases where a cache is needed, while sparse flat caching is useful when avoiding runtime allocations is important.

For our first MPU6500 tutorial we will use:  
`REGCACHE_MAPLE`,  maple-tree-backed register cache type (Modern Linux).

Once cached, `regcache_sync()` pushes all dirty cached values back out to hardware in one pass,     
extremely useful after a device comes out of a low-power state and needs to be reprogrammed.

### Why Is Caching Useful?
Consider a configuration register:   
`PWR_MGMT_1`     
The driver may write:    
```c
 regmap_write(map, MPU6500_REG_PWR_MGMT_1, value);
```

If the driver later needs the configured value, it may be possible to obtain it from the cache rather than accessing the hardware again.     

Caching becomes especially useful when:  
* There are many configuration registers
* Hardware access is relatively slow
* The device is frequently suspended/resumed
* The driver needs to restore register configuration
* The device loses register state during power transitions

### Regmap and Power Management
**Regmap caching** is particularly useful in **power-management** scenarios.     
```
            Normal Operation Driver 
                    | 
                  Regmap 
                    | 
                  Hardware 
                    | 
                    v 
            Suspend / Power Off Driver 
                    | 
                Regmap Cache 
                    | 
                Hardware OFF
```

When the hardware comes back, the driver can synchronize the cached configuration back to the device.    

This is one reason Regmap is commonly used by complex Linux drivers.     

### Regmap and Endianness    

For example, a device might use:

**Big endian** or **Little endian** for multi-byte values.

The urrent `regmap_config` includes fields for register-format and value-format endianness.

This is important for devices where a register value is larger than one byte.

For example (16-bit register value):
```
+--------+--------+ 
| Byte 1 | Byte 2 | 
+--------+--------+ 
    MSB     LSB
```
The driver needs to interpret the bytes correctly.

### Regmap Fields
Sometimes a register contains several independent bit fields.    
For example:     
Register `0x10`  
```
+----+----+----+----+----+----+----+----+ 
|  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 | 
+----+----+----+----+----+----+----+----+ 
|         MODE      |      ENABLE       |  
+-------------------+-------------------+
```

Instead of repeatedly using masks manually, Regmap provides:     
struct regmap_field:     
`regmap_field_read();`     
`regmap_field_write();`    
`regmap_field_update_bits();`

This is useful for large devices with many bit fields.

### Regmap Register Ranges
Some devices use a paged or banked register architecture.

```
Page 0                 Page 1                 Page 2     
+---------------+      +---------------+      +---------------+
| Register 0x00 |      | Register 0x00 |      | Register 0x00 |
| Register 0x01 |      | Register 0x01 |      | Register 0x01 |
| Register 0x02 |      | Register 0x02 |      | Register 0x02 |
+---------------+      +---------------+      +---------------+
```
The driver may need to: 
1. Select a page
2. Access a register inside that page

Regmap supports this through register ranges.    

The current API includes struct `regmap_range_cfg` for indirectly accessed or paged registers.

This allows a driver to present a virtual register space while Regmap handles the page-selection mechanism.


### Regmap Register Sequences

Some devices require several registers to be written in a specific order.    
For example:     
```
Write register A 
       | 
       v 
  Wait 100 us 
       | 
       v 
Write register B 
       | 
       v 
Write register C
```

Regmap provides register-sequence support through:   
`struct reg_sequence`    
A sequence can contain: **Register**, **Value**, **Delay**

This is useful when initializing complex hardware.   


## How Regmap Is Initialized

For an **I²C** device, the common managed initialization function is:

`devm_regmap_init_i2c()`     

For example *MPU6500*:
```c
data->regmap = devm_regmap_init_i2c( client, &mpu6500_regmap_config );
```
```
i2c_client 
    | 
    | 
    v 
devm_regmap_init_i2c() 
    | 
    v 
struct regmap 
    | 
    v 
Regmap subsystem
```

The `devm_` prefix means this is a device-managed resource. The kernel's device-resource management system automatically releases managed resources when the device is detached.

## Regmap Supports Multiple Buses

Regmap is not limited to **I²C**.

The kernel Regmap infrastructure supports multiple bus types.

Examples include:    
```
I2C SPI I3C MMIO MDIO SCCB SPMI SLIMbus
```
The driver can therefore use the same basic register-access API while the underlying transport differs.

I2C Device: 
```
devm_regmap_init_i2c() 
    | 
    v 
regmap_read()
```

SPI Device:
```
devm_regmap_init_spi() 
    | 
    v 
regmap_read()
```

The current kernel `regmap.h` exposes bus-specific initialization interfaces for several device types.

## Regmap Use Cases

Regmap is particularly useful when a Linux device has a register-based control interface.    
Common examples include:     
* Sensors
    ```
    Accelerometer 
    Gyroscope 
    Temperature Sensor 
    Pressure Sensor 
    ADC
    ```
* Audio Devices
    ```
    Audio Codec
    DAC
    ADC
    Amplifier
    ```
    Audio devices often contain hundreds of configuration registers.

    Regmap provides a convenient way to access them. 
* PMICs
    ```
    Voltage 
    Current 
    Power 
    states 
    Thermal 
    protection 
    Interrupts
    ```
    Regmap is very useful for these devices.
* GPIO Expanders
    An I²C or SPI GPIO expander may contain registers such as:
    ```
    Input 
    Output 
    Direction 
    Pull-up 
    Pull-down 
    Interrupt
    ```
    These can naturally be represented using Regmap.
* Clock Controllers
    Clock devices often expose registers controlling:
    ```
    Clock enable 
    Clock divider
    Clock source 
    PLL configuration
    ```
* Display and Multimedia Devices
    Many display controllers and multimedia ICs expose large register maps.  
    Regmap can simplify their register access.   

## When Should You Use Regmap?

Regmap is a good choice when:    
```
The device has registers
        +
The registers are accessed through a supported bus
        +
The driver needs normal register operations  
```
For example:
```
MPU6500 
    | 
    +--Register based 
    | 
    +-- I2C 
    | 
    +-- Read/write registers 
    | 
    +-- Bulk register access 
    | 
    +-- Bit fields 
    | 
    +-- Configuration registers 
    | 
    v 
Regmap = Good fit
```

## When Might Regmap Not Be Appropriate?

Regmap is an abstraction for register-based devices.     
It may not be the best abstraction when a device has a completely unusual communication protocol that cannot naturally be represented as register operations.    
For example, if communication consists of complex commands such as:  
```
Command 
Length 
Payload 
CRC 
Response 
Variable-length 
protocol
```
with no meaningful register model, direct bus APIs or another subsystem may be more appropriate.     
The Regmap API itself allows custom register read/write operations for hardware whose access mechanism cannot be represented as a normal bus register operation.

### Prerequisites

Make sure you've watched:

* [T22: Sysfs Interface](../22_sysfs)
* [T27: BMP180 I2C Sensor with a Device Tree Overlay](../27_dt_i2c)


This tutorial assumes a Raspberry Pi with I2C enabled (`dtparam=i2c_arm=on` in `/boot/config.txt`) and a device connected on I2C bus 1.

## Summary Table
Function|Purpose
--------|--------
`devm_regmap_init_i2c()`|Allocate and bind a managed regmap instance to an I2C client
`regmap_read()`|Read a single register, honoring cache/volatile rules
`regmap_write()`|Write a single register, updating the cache
`regmap_update_bits()`|Read-modify-write a subset of bits in a register
`regcache_sync()`|Push all dirty cached registers back to hardware
`regmap_config.volatile_reg`|Callback marking registers that must bypass the cache


Makefile Update:

```Makefile
load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d ./dts $(project_name)
	@echo "Installing module into modules tree..."
	sudo mkdir -p /lib/modules/$(shell uname -r)/extra
	sudo cp $(project_name).ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	@echo "Loading regmap-i2c dependency and module..." 
	sudo modprobe regmap-i2c
	sudo modprobe $(project_name)

unload:
	@echo "Unload the kernel module..."
	sudo modprobe -r $(project_name)
	@echo "remove DT overlay..."
	sudo dtoverlay -r $(project_name)
	@echo "remove copy of $(project_name).ko"
	find /lib/modules/$(uname -r) -name "$(project_name).ko*"
	sudo rm -f /lib/modules/$(uname -r)/extra/$(project_name).ko
	sudo depmod -a
```

* `depmod -a` rebuilds the module dependency database (`modules.dep`) so the kernel knows our new `.ko` exists and what it depends on.
* That's what lets `modprobe` (rather than `insmod`) resolve and auto-load prerequisite modules.
* Since our driver calls `devm_regmap_init_i2c()`, it has a real symbol dependency on the `regmap-i2c` module - `MODULE_SOFTDEP("pre: regmap-i2c")` in the driver just hints the order, but `regmap-i2c` still has to actually be loaded (or auto-loaded) first, and `depmod -a` is what makes that dependency resolvable via modprobe.

## Terminal commands

```bash


sudo cat /sys/kernel/debug/regmap/1-0068/registers 

cat /sys/bus/i2c/devices/1-0068/sensor_read
echo 0 | sudo tee /sys/bus/i2c/devices/1-0068/set_grconf


```

* I2C ftrace tracepoints: Enable i2c bus tracing
```bash
sudo su
cd /sys/kernel/debug/tracing
# Enable only the i2c event class
echo 1 > events/i2c/enable
# Tracing shell
cat trace
# Clear any old trace data:
echo > trace
# Combine clearing and reading
sudo sh -c 'echo > trace && cat trace_pipe'
```



