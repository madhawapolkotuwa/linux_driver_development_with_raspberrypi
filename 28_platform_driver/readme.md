# Platform Driver

>> New here? This tutorial assumes you're already comfortable with kernel modules, character devices, and the Linux Device Model (Tutorial 21). If any of that sounds unfamiliar, check the earlier videos first.

We've already used platform drivers a few times in this series 
- sysfs(Tutorial 22), debugfs(Tutorial 23) 
- Device Tree Overlays (Tutorial 25)
- the GPIO LED/button DT overlay (Tutorial 26) 
- I2C with Device Tree overlay (Tutorial 27), etc
all relied on the platform driver framework under the hood.      
This tutorial takes a step back and explains what the platform bus actually is, and why `platform_device` and `platform_driver` exist in the first place.

### Why does the platform bus exist?

Buses like **USB**, **PCI**, and **I2C** are discoverable - That means when we plug in a USB, or scan an I2C bus, and the bus hardware itself can tell the kernel "hey, something new is here, and here's some information about it"        
The kernel can then go find a matching driver automatically.

A lot of hardware doesn't work that way. Things like UART controllers, GPIO controllers, RTCs, and other peripherals built directly into the `SoC` don't announce themselves to anything - they're just memory-mapped registers sitting at a fixed address. There's no bus hardware to ask "what's connected here?"

So the kernel needed a way to represent these devices anyway, using the same device model concepts we use everywhere else, like buses, devices, and drivers.
That's exactly what the platform bus is. It's a virtual bus.
It doesn't correspond to any real physical bus hardware. It only exists inside the kernel, so that these non discoverable devices can still be represented properly.

This is also why the platform bus is useful outside of **I2C/SPI** devices. **I2C** and **SPI** already have their own bus types with their own discovery/matching rules. The platform bus is for everything that doesn't fit under a real bus - onboard controllers, memory-mapped peripherals, and general "glue" devices.

### `platform_device` vs `platform_driver`

|        | `platform_device` | `platform_driver` |
--------|-------------------|-------------------|
Represents | The hardware / the device itself | The code that knows how to operate that hardware |
Registered with | `platform_device_register()` | `platform_driver_register()` / `module_platform_driver()` |
Holds   | 	Name, resources (IRQs, memory regions), `platform_data` | `probe()`, `remove()`, matching table |
Matching key (classic) | `.name` field | `.driver.name` or `id_table` |
Matching key (Device Tree) | Node's `compatible` property | `of_match_table` |
Can exist without the other? | Yes, but does nothing until a driver binds | Yes, but `probe()` never runs until a device shows up |

The important idea: **the device and the driver are two separate objects that the kernel matches together.** Registering one doesn't do anything by itself - `probe()` only runs once both sides exist and their names (or compatible strings) line up.      

That matching logic is exactly what we've been relying on every time we added an of_device_id table in the Device Tree tutorials([26_dt_gpio](../26_dt_gpio/src/gpio_dt_driver.c#L215), [27_dt_i2c](../27_dt_i2c/src/bmp180_i2c_dt_driver.c#L355)).

And we use the same module file to register the platform driver and the platform device ([22_sysfs](../22_sysfs/example/sysfs_ex.c#L336), [23_debugfs](../23_debugfs/mydrv.c#L434)).

 - this tutorial just shows the non-DT version of the same mechanism, two separate files for the platform device and driver.

### Code walkthrough

This example intentionally avoids real hardware and Device Tree, so you can see the platform bus mechanics in isolation.    
Two modules are involved:    

`my_platform_device.c`:  
Registers a `platform_device` named `my_platform_dev` with a small `platform_data` struct attached (`label` and `value`).   
This struct is how the device side hands private data over to whichever driver ends up binding to it.

A `release()` callback is also provided. The driver core requires this - it's called once the device's reference count hits zero, so the kernel knows it's safe to release it. We're not allocating anything dynamically here, so it just logs a message.

`my_platform_driver.c`:  
Registers a `platform_driver` whose `driver.name` is `my_platform_dev` - matching the device's `.name` field exactly. When both modules are loaded, the driver core sees the matching names and calls `probe()`.

Inside `probe()`, we pull the `platform_data` back out using `dev_get_platdata()` and print it, to show that data really did travel from the device side to the driver side through the platform bus.


 An `id_table` is also included, even though we only have one device name. It's not required for a single-device driver, but it's worth introducing now - it's how one driver can bind to several device name variants and get different `driver_data` per variant, which becomes useful once you're supporting multiple hardware revisions.




