# Writing Raspberry Pi Device Tree Overlays

## Video :-

[![Youtube Video](https://img.youtube.com/vi/kYs5COiMf6M/0.jpg)](https://www.youtube.com/watch?v=kYs5COiMf6M)

## Overview

In the [previous video](../24_device_tree_intro/readme.md), we got a high-level introduction to what the **Device Tree** is and how it describes hardware to the Linux kernel.     
Now it is time to go deeper and get hands-on.

In this vodeo, we will write a **Device Tree Overlay**,   
A small patch that adds a new device node to the running Device Tree without modifying the board's base `.dtb` file.    
We will cover the overlay file structure, compile it using `dtc`, apply it live on the Raspberry Pi, and then access all of its properties from inside a platform driver module.

---------------------

## What Is a Device Tree Overlay?

The base **Device Tree Blob** (`.dtb`) for Raspberry Pi, NXP, Radxa, etc.. is compiled at build time and describes all the hardware that ships with the board.     
A **Device Tree Overlay** (`.dtbo`) is an additional, independently compiled fragment that gets merged into the live Device Tree at runtime.     
This makes overlays the standard way to:
* Describe custom hardware connected to the board (sensors, displays, HATs)
* Enable or disable peripherals without rebuilding the kernel
* Inject custom properties that a loadable driver module can read

>> **Note:** On Raspberry Pi OS, etc, overlays placed in `/boot/firmware/overlays/` and listed in `/boot/firmware/config.txt` are applied automatically at boot. We can also apply them manually at runtime using the dtoverlay tool, which is what we will do in this tutorial.

( We talked about these in the [last video](../24_device_tree_intro/) in detail. )

-------

### Step 1: The Device Tree Overlay File

#### 1.1 Expanded Overlay : Multiple Property Types

Below is the overlay used in this tutorial. It is intentionally designed to demonstrate some of the common DT property data types defined in the Device Tree Specification.

`testoverlay.dts`:
```c
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2835";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            my_device {
                compatible = "brightlight,mydev";
                status = "okay";

                /* String property */
                label = "TestDevice";

                /* 32-bit unsigned integer */
                my_value = <12>;

                /* 8-bit unsigned integer */
                my_small_val = /bits/ 8 <0x42>;

                /* Boolean property — presence means true */
                my_flag;

                /* Array of 32-bit integers */
                my_array = <10 20 30 40>;

                /* List of strings */
                my_stringlist = "alpha", "beta", "gamma";
            };
        };
    };
};
```

#### 1.2 DT Property Types Summary

Property | DT | Syntax | Kernel API C Type |
---------|----|--------|--------------------|
**label**  | `"string"` | `device_property_read_string()` | `const char *`
**my_value** | `<12>` | `device_property_read_u32()` | `u32`
**my_small_val** | `/bits/ 8 <0x42>` | `device_property_read_u8()` | `u8`
**my_flag** | (no value) | `device_property_present()` | boolean check
**my_array** | `<10 20 30 40>` | `device_property_read_u32_array()` | `u32[]`
**my_stringlist** | `"alpha"`, `"beta"`, `"gamma"` | `device_property_read_string_array()` | `const char *[]`

### Step 2: Compiling the Overlay

The Device Tree Compiler (`dtc`) compiles the `.dts` source into a binary `.dtbo` blob.  
The `-@` flag is required for overlays, it preserves symbol information so the overlay can be correctly merged into the base tree at runtime.

```bash
dtc -@ -I dts -O dtb -o testoverlay.dtbo testoverlay.dts
```
|           |                 |
|-----------|-----------------|
|  `-@`     |   Enable symbols (required for overlays)
|`-I dts`   | Input format: DTS source
| `-O dtb`  | Output format: DTB binary
| `-o testoverlay.dtbo` | Output file

>> You may see warnings about missing `#address-cells` or `#size-cells`, these are safe to ignore for simple property-only nodes like ours.


>> Usually we add this command to `Makefile`:
```makefile
obj-m += dt_probe.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o testoverlay.dtbo testoverlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.dtbo
```

### Step 3: Apply the Overlay Live and Remove

```bash
sudo dtoverlay -d . testoverlay  # or sudo dtoverlay testoverlay.dtbo

# to remove
sudo dtoverlay -r testoverlay
```
or
```bash
sudo mkdir -p /sys/kernel/config/device-tree/overlays/myoverlay
sudo cp testoverlay.dtbo /sys/kernel/config/device-tree/overlays/myoverlay/dtbo

# to remove
sudo rmdir /sys/kernel/config/device-tree/overlays/myoverlay
```
List all active overlays:
```bash
dtoverlay -l
```

>> Note: usually we also do this in the `Makefile`
```makefile
obj-m += dt_probe.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o testoverlay.dtbo testoverlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d . testoverlay
	@echo "load the the kernel module..."
	sudo inmsmod dt_probe.ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod dt_probe
	@echo "remove DT overlay..."
	sudo dtoverlay -r testoverlay
```

# Summary

#### Overlay Structure 
```
/dts-v1/;          ← DTS version marker
/plugin/;          ← Marks this file as an overlay (not a full tree)

/ {
    compatible = "brcm,bcm2835";    ← Target platform guard

    fragment@0 {                    ← Each fragment patches one target
        target-path = "/";          ← Which node to patch (root here)
        __overlay__ {               ← Content to merge in
            my_device { ... };      ← New node added to the tree
        };
    };
};
```

Key points:

* `/plugin/;` tells dtc this is an overlay, required for the `-@` flag to work correctly
* `fragment@0` wraps each discrete patch, we can have multiple fragments targeting different nodes
* `target-path` specifies the path of the existing node to attach to, `target = <&label>` can be used instead when the node has a label
* `__overlay__` contains the actual nodes and properties that get merged in