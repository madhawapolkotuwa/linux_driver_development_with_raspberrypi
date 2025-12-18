# ✅ Linux Driver Development with Raspberry Pi — Full Tutorial Series Roadmap


## SECTION 0 — Introduction & Preparation

### 1. Introduction to the Series
* What the series covers
* Tools, requirements
* Recommended references (inc. Linux Device Drivers 3rd Edition)

### 2. Understanding the Linux Kernel 
* Kernel architecture
* User space vs kernel space

### 3. Linux Device Drivers Overview


## SECTION 1 — Raspberry Pi Setup & Development Environment

### [4. Raspberry Pi Setup](/04_raspberry_pi_setup/)
* Installing Raspberry Pi OS (64-bit)
* Enabling SSH
* Installing kernel headers
* Recommended directory layout

### [5. First Kernel Module](/05_first_kernel_module/01_hello/)
* A simple hello.ko
* Makefile
* insmod, rmmod, modinfo
* [Kernel print levels (Log levels)](/05_first_kernel_module/02_log_levels/)

## SECTION 2 — Character Device Driver Foundations

### [6. Character Device Driver Basics](/06_character_device_driver/)
* Allocating major/minor numbers
* cdev_init, cdev_add
* Manual mknod vs udev auto-creation
* File operations overview

### [7. File Operations Deep Dive](/07_file_cdev_fops/)
* [open, release, read, write](/07_file_cdev_fops/01_file_ops/)
* copy_to_user, copy_from_user
* [Using file->private_data](/07_file_cdev_fops/02_private_data/)

### 8. IOCTL Implementation
* Defining IOCTL commands
* Handling IOCTL in driver
* User application examples

### 9. Memory Allocation Mechanisms
* kmalloc, kzalloc, vmalloc
* GFP flags
* Slab allocator overview

### 10. Kernel Linked Lists
* list_head, iterating, adding, deleting
* Using linked lists in drivers

## SECTION 3 — GPIO, Interrupts, Timers

### 11. GPIO Control
* Request/release GPIO
* Set/get GPIO state
* Debouncing

### 12. GPIO Interrupt Handling
* IRQ numbers
* request_irq
* Top-half vs bottom-half
* Tasklets vs workqueues

### 13. High Resolution Timers (hrtimer)
* One-shot vs periodic timers
* Callback examples

## SECTION 4 — I2C & SPI Drivers

### 14. I2C Subsystem Basics
* Linux I2C architecture
* Client vs adapter vs driver
* Registering an I2C driver
* Creating a device via /sys/bus/i2c/devices

### 15. BMP180 Sensor Driver (Complete Example)
* Read/write registers
* Calibration
* Temperature & pressure reading
* Exposing sensor values to user space

### 16. SPI Subsystem Basics
* SPI architecture
* Full duplex transfers
* SPI message & transfer structs

## SECTION 5 — Concurrency & Synchronization

### 17. Kernel Threads (kthread)
* Creating, running, stopping threads
* Thread loop best practices

### 18. Wait Queues, Poll, Fasync
* Blocking/non-blocking I/O
* Implementing .poll()
* Async notification using fasync

### 19. Kernel Synchronization Techniques
* Mutex
* Spinlock
* Completion
* When to use which

### 20. Workqueues & Bottom Halves
* Deferred execution
* System vs dedicated workqueues
* Comparison with tasklets

## SECTION 6 — Linux Device Model & Sysfs

### 21. Linux Device Model
* Devices, drivers, buses
* Probe/remove lifecycle
* Uevents (plug/unplug)

### 22. Sysfs Interface
* Device attributes
* Creating and removing sysfs files
* Sysfs class interfaces (/sys/class/mydriver)

### 23. Debugfs for Driver Debugging
* Creating debugfs entries
* Live introspection of driver variables
* Differences from sysfs

## SECTION 7 — Device Tree & Raspberry Pi Overlays

### 24. Device Tree Introduction
* Why Device Tree exists
* Structure of .dts and .dtsi
* Status, compatible, reg, gpios

### 25. Writing Raspberry Pi Device Tree Overlays
* Overlay structure
* Adding nodes dynamically
* Applying overlays live

### 26. Device Tree — GPIO Example
* Creating a GPIO LED/button node
* Binding to your driver
* Reading GPIO from DT

### 27. Device Tree — I2C Device
* Adding BMP180 in DT
* Custom compatible string
* Passing configuration via DT

### 28. Platform Driver (Optional but Recommended)
* Introduction to platform bus
* Platform_device vs platform_driver
* Useful for non-I2C/SPI devices

### 29. Pin Control & Pinmux
* Pin configuration in DT
* Alternate functions on Raspberry Pi pins

### 30. OF (Open Firmware) Parsing
* Using DT helper APIs
* Parsing strings, ints, gpios from DT

## SECTION 8 — Advanced Topics

### 31. Misc Device Driver
* Easiest way to expose /dev/mydevice
* When to prefer over char device

### 32. Regmap API
* Excellent for I2C/SPI register-mapped chips
* Reduces boilerplate
* Cache & sync

### 33. DMA Engine Basics & DMA memcpy
* DMA vs CPU copy
* DMA mapping APIs
* Simple DMA example

### 34. Accessing Files from Kernel (Safely)
* filp_open, kernel_read, kernel_write
* Kernel warnings & limitations
* Practical example: writing logs to SD card

### 35. Memory Mapping (mmap)
* Expose kernel buffers to user space
* Example: custom framebuffer
* Page faults inside driver

### 36. Industrial I/O (IIO) Framework
* When to use IIO
* Creating an IIO device
* Reading ADC-like data

### 37. Sending Signals to User Space
* Real-time signals
* Notify app when an event occurs
* Example: IRQ → signal to application

## SECTION 9 — Serial Drivers
### 38. UART (Serial) Driver
* UART on Raspberry Pi
* Writing a minimal UART driver
* Using termios from user space

## SECTION 10 — Final Project
### 39. Complete Raspberry Pi Kernel Driver Project
* A sophisticated final driver combining everything:
* Device Tree overlay
* Char device with IOCTL
* Sysfs interface
* GPIO + interrupt + poll
* High-resolution timer
* Optional I2C or SPI communication
* debugfs runtime variables
* User-space test application

### 40. Packaging & Publishing
* Versioning
* DKMS support
* GitHub best practices
* Share as a Linux kernel module package