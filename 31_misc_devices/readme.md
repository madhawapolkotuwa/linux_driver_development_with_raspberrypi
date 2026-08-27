# Misc Devices

## Video :-

[![Youtube Video](https://img.youtube.com/vi/31haM9OFkrA/0.jpg)](https://www.youtube.com/watch?v=31haM9OFkrA)

A **misc device** (miscellaneous device) is a simplified character device interface in the Linux kernel that lets a driver register a `/dev` node **without owning a dedicated major number**.  
All misc devices share a single major number  **`10`**  and are distinguished from each other by a unique minor number, which the kernel can either assign statically or allocate dynamically.

## Why Misc Devices Exist
Every character device is normally identified by a **major:minor** pair:
* **Major number**: identifies the driver (or driver class)
* **Minor number**: identifies the specific device instance

Allocating and managing a major number is overkill for drivers that expose a single, simple device node.     
GPIO test interfaces, debug interfaces, small sensors, watchdogs, RTCs, etc.     
The kernel provides the misc framework (`drivers/char/misc.c`, `<linux/miscdevice.h>`) so these drivers can register a character device in a few lines, without touching `alloc_chrdev_region()`, `cdev_init()`, or `class_create()` directly.   

## Key Features of Misc Devices

1. Shared Major Number
All misc devices share:
```
Major = 10
```
2. Minor Number Allocation: Static or Dynamic    
A misc device can request:

    * A fixed minor number, if it needs to match a well-known `/dev` name (e.g. `/dev/rtc`), or
    * `MISC_DYNAMIC_MINOR`, letting the kernel assign the next free minor automatically: the common choice for custom drivers.

    Examples of existing misc devices:
    ```
    10, 130 watchdog
    10, 229 fuse
    10, 258 udmabuf
    ```

## Minimal Registration API

Instead of the full character-device sequence, a misc driver only needs to fill in a struct `miscdevice` and call one function:
```c
struct miscdevice {
    int minor;
    const char *name;
    const struct file_operations *fops;
    ...
};

// Register
misc_register(<pass the Misc device structure address>);

// Unregister
misc_deregister(<pass the Misc device structure address>);
```
Standard `file_operations` (*open*, *read*, *write*, *unlocked_ioctl*, *release*, …) are implemented exactly as they would be for a normal char driver.  
The misc framework only removes the boilerplate around **major/minor** and **class/device** creation.

## Automatic `/dev/<name>` Node Creation

`misc_register()` internally creates a device under the kernel's misc class, and udev picks that up to create `/dev/<name>` automatically, no manual `device_create()` call needed.

## Clean 
On module removal, `misc_deregister()` unwinds the registration and removes the device node.








