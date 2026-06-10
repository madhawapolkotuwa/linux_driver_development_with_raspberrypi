# ✅ Linux Driver Development with Raspberry Pi - Full Tutorial Series Roadmap


## SECTION 0 - Introduction & Preparation

### 1. Introduction to the Series
* What the series covers
* Tools, requirements
* Recommended references (inc. Linux Device Drivers 3rd Edition)

### 2. Understanding the Linux Kernel 
* Kernel architecture
* User space vs kernel space

### 3. Linux Device Drivers Overview


## SECTION 1 - Raspberry Pi Setup & Development Environment

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

## SECTION 2 - Character Device Driver Foundations

### [6. Character Device Driver Basics](/06_character_device_driver/)
* Allocating major/minor numbers
* cdev_init, cdev_add
* Manual mknod vs udev auto-creation
* File operations overview

### [7. File Operations Deep Dive](/07_file_cdev_fops/)
* open, release, read, write
* copy_to_user, copy_from_user
* Using file->private_data

### [8. IOCTL Implementation](/08_ioctl/)
* Defining IOCTL commands
* Handling IOCTL in driver
* User application examples

### [9. Memory Allocation Mechanisms](/09_memory_alloc/)
* kmalloc, kzalloc, vmalloc
* GFP flags
* Slab allocator overview

### [10. Kernel Linked Lists](/10_linked_list/)
* list_head, iterating, adding, deleting
* Using linked lists in drivers

## SECTION 3 - GPIO, Interrupts, Timers

### [11. GPIO Control](/11_gpio_ctrl/)
* Request/release GPIO
* Set/get GPIO state

### [12. GPIO Interrupt Handling](/12_gpio_irq/)
* request_irq
* IRQ numbers
* Debouncing
* Top-half vs bottom-half

### [13. High Resolution Timers (hrtimer)](/13_hrtimer/)
* One-shot, periodic timers

## SECTION 4 - I2C & SPI Drivers

### [14. I2C Subsystem Basics](/14_i2c_subsystem_basics/)
* Linux I2C architecture
* Client vs adapter vs driver
* Registering an I2C driver
* Creating a device via /sys/bus/i2c/devices

### [15. BMP180 Sensor Driver (Complete Example)](/15_bmp180_i2c_sensor/)
* Read/write registers
* Calibration
* Temperature & pressure reading
* Exposing sensor values to user space

### [16. SPI Subsystem Basics](/16_spi_subsytem_basics/)
* SPI architecture
* Full duplex transfers
* SPI message & transfer structs

## SECTION 5 - Concurrency & Synchronization

### [17. Kernel Threads (kthread)](/17_kernel_threads/)
* Creating, running, stopping threads
* Thread loop best practices

### [18. Wait Queues, Poll, Fasync](/18_wait_queue_poll_fasync/)
* [Blocking/non-blocking I/O](/18_wait_queue_poll_fasync/02_wait_queue/)
* [Implementing .poll() & select()](/18_wait_queue_poll_fasync//03_poll_select/)
* [Async notification using fasync](/18_wait_queue_poll_fasync/04_fasync/)

### [19. Kernel Synchronization Techniques](/19_kernel_synchronization/)
* [Mutex](/19_kernel_synchronization/01_mutex/)
* [Spinlock](/19_kernel_synchronization/02_spinlock/)
* [Completion](/19_kernel_synchronization/03_completion/)
* [When to use which](/19_kernel_synchronization/When_to_use_which/)

### [20. Workqueues & Bottom Halves](/20_workqueue_and_bottom_halves/)
* Deferred execution
* System vs dedicated workqueues
* Comparison with tasklets

## SECTION 6 - Linux Device Model & Sysfs

### [21. Linux Device Model](/21_linux_device_module/)
* Devices, drivers, buses, classes
* Probe/remove lifecycle
* Hotplug & Uevents (plug/unplug)

### [22. Sysfs Interface](/22_sysfs/)
* Device attributes
* Creating and removing sysfs files
* Sysfs class interfaces (/sys/class/mydriver)

### [23. Debugfs for Driver Debugging](/23_debugfs/)
* Creating debugfs entries
* Live introspection of driver variables
* Differences from sysfs

## SECTION 7 - Device Tree & Raspberry Pi Overlays

### [24. Device Tree Introduction](/24_device_tree_intro/)
* Why Device Tree exists
* Structure of .dts and .dtsi
* Status, compatible, reg, gpios

### 25. Writing Raspberry Pi Device Tree Overlays
* Overlay structure
* Adding nodes dynamically
* Applying overlays live

### 26. Device Tree - GPIO Example
* Creating a GPIO LED/button node
* Binding to your driver
* Reading GPIO from DT

### 27. Device Tree - I2C Device
* Adding BMP180 in DT
* Custom compatible string
* Passing configuration via DT

### 28. Platform Driver 
* Introduction to platform bus
* Platform_device vs platform_driver
* Useful for non-I2C/SPI devices

### 29. Pin Control & Pinmux
* Pin configuration in DT
* Alternate functions on Raspberry Pi pins

### 30. OF (Open Firmware) Parsing
* Using DT helper APIs
* Parsing strings, ints, gpios from DT

## SECTION 8 - Advanced Topics

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

## SECTION 9 - Serial Drivers
### 38. UART (Serial) Driver
* UART on Raspberry Pi
* Writing a minimal UART driver
* Using termios from user space

## SECTION 10 - Final Project
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