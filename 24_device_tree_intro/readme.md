# Device Tree (DT)

## Video :-
[![Youtube Video](https://img.youtube.com/vi/_kgXgLwQymo/0.jpg)](https://www.youtube.com/watch?v=_kgXgLwQymo)

The **Device Tree** is a data structure used by the Linux kernel to describe hardware that cannot be automatically discovered - such as peripherals on embedded platforms like Raspberry Pi, NXP, Radxa, etc.     
Instead of hard-coding board-specific hardware details into the kernel source, the Device Tree externalizes this information into a human-readable text format (`.dts`) that is compiled into a binary blob (`.dtb`) and passed to the kernel at boot time.

This tutorial covers:

* Why the Device Tree exists and the problem it solves
* The structure of `.dts` and `.dtsi` files
* Core properties: `status`, `compatible`, `reg`, `gpios`, `interrupts`, `clocks`, and more
* How to read and navigate a real Raspberry Pi Device Tree

-------------------------------------------------------------------------------------
* [Device trees everywhere](https://ozlabs.org/people/dgibson/papers/dtc-paper.pdf)
* [Device tree specifications](https://www.devicetree.org/specifications)

## 1. Why Does the Device Tree Exist?
**The Problem: Board-Specific Code in the Kernel**   

On x86 PCs, hardware can be discovered dynamically via buses like PCI and USB. The OS interrogates the bus and the devices identify themselves.  

On embedded platforms (ARM, RISC-V, etc.), peripherals are typically memory-mapped and have no self-description mechanism.   
Historically, Linux handled this with board support files -> C files in `arch/arm/mach-*/` that hard-coded every platform's hardware layout:
```c
/* Old way -> hard-coded board file (arch/arm/mach-bcm2708/bcm2708.c) */
static struct resource uart0_resources[] = {
    {
        .start = 0x20201000,
        .end   = 0x20201FFF,
        .flags = IORESOURCE_MEM,
    },
};
```

This led to thousands of board files being merged into the kernel - a maintenance nightmare.     


**The Solution: Device Tree**    

The Device Tree, borrowed from Open Firmware (used in Sun SPARC and PowerPC systems), provides a standardized, platform-independent way to describe hardware in a tree-structured text file:

- The kernel no longer needs board-specific C code for hardware layout
- The same kernel image can boot on multiple boards with different `.dtb` files
- Hardware description is maintained outside the kernel source tree
- Firmware (bootloader) passes the `.dtb` to the kernel at boot

On Raspberry Pi, the bootloader (`start.elf` / `start4.elf`) loads the appropriate `.dtb` file and passes its address to the kernel via the ARM boot protocol.

```
Boot sequence:
  bootloader → loads kernel image + .dtb → kernel parses .dtb → drivers probe devices
```
## 2. DTS File Structure

| Extension | Description |
|-----------|-------------|
| `.dts`      | Device Tree Source - human-readable, top-level board file
| `.dtsi`     | Device Tree Source Include - reusable fragments (SoC definitions)
| `.dtb`      | Device Tree Blob - compiled binary passed to the kernel
| `.dtbo`     | Device Tree Blob Overlay - loadable overlay for runtime modification

The typical layering on Raspberry Pi:
```
bcm2711-rpi-4-b.dts          ← board-level (Raspberry Pi 4B)
    └── bcm2711.dtsi          ← SoC-level (BCM2711 peripherals)
            └── bcm2711-rpi.dtsi  ← common RPi definitions
```

```bash
# Compiled .dtb files live in /boot/firmware (RPi OS Bookworm+)
ls /boot/firmware/*.dtb
ls /boot/firmware/overlays/*.dtbo

# On older RPi OS releases
ls /boot/*.dtb
ls /boot/overlays/*.dtbo
```

### 2.2 Basic DTS Syntax

**Decompile a `.dtb` Back to Human-Readable `.dts`**
```bash
# Install device tree compiler - if not installed
sudo apt install device-tree-compiler

# Decompile the running board's .dtb
dtc -I fs -O dts -s /sys/firmware/devicetree/base > rpi4b.dts
```

A `.dts` file describes hardware as a tree of nodes and properties:

```json
/dts-v1/;                          /* DTS format version — always required */

/ {                                /* Root node */
    model = "Raspberry Pi 4 Model B";
    compatible = "raspberrypi,4-model-b", "brcm,bcm2711";

    #address-cells = <1>;          /* How many cells describe an address */
    #size-cells = <1>;             /* How many cells describe a size */

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a72";
            reg = <0>;             /* CPU index */
        };
    };

    memory@0 {
        device_type = "memory";
        reg = <0x0 0x40000000>;    /* Base address + size (1 GB) */
    };

    soc {                          /* System-on-Chip node */
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;                    /* Identity mapping of addresses */

        uart0: serial@7e201000 {
            compatible = "brcm,bcm2835-aux-uart";
            reg = <0x7e201000 0x200>;
            status = "okay";
        };
    };
};
```

Key syntax rules:

* Every node has the form `name@unit-address { ... };`
* The `@unit-address` must match the first value of the reg property
* Properties are `key = <value>;` for cells or `key = "string";` for strings
* Labels (`uart0:`) create phandles for cross-referencing nodes

-----------

## 3. Core Properties

### 3.1 `compatible`
The most important property. It tells the kernel which driver to bind to the device. It is a list of strings ordered from most specific to most generic:

```json
compatible = "brcm,bcm2835-i2c", "brcm,bcm2708-i2c";
/*            ^-- specific model   ^-- generic fallback */
```

The kernel's driver matching logic:

1. The driver declares an `of_match_table` with compatible strings it handles
2. The kernel walks the Device Tree and calls the driver's `probe()` when a match is found

```c
/* Driver side */
static const struct of_device_id my_i2c_ids[] = {
    { .compatible = "brcm,bcm2835-i2c" },
    { .compatible = "brcm,bcm2708-i2c" },
    { }  /* sentinel */
};
MODULE_DEVICE_TABLE(of, my_i2c_ids);
```

### 3.2 `status`

Controls whether the kernel considers the node active:

```json
status = "okay";     /* Device is enabled — driver will probe it */
status = "disabled"; /* Device is disabled — driver will NOT probe */
status = "reserved"; /* Device is reserved for another purpose */
status = "fail";     /* Device has failed, do not use */
```

Default when omitted is `"okay"`. Overlays commonly use `status = "okay"` to enable peripherals:

```json
/* In a .dtbo overlay to enable SPI1 */
&spi1 {
    status = "okay";
};
```

3.3 `reg`
Describes memory-mapped register ranges or device addresses. Its format is defined by the parent node's `#address-cells` and `#size-cells`:

```json
/* Parent says: 1 cell for address, 1 cell for size */
#address-cells = <1>;
#size-cells = <1>;

uart0: serial@7e201000 {
    reg = <0x7e201000 0x200>;
    /*     ^base addr  ^size (512 bytes) */
};

/* For I2C devices: address only, no size (size-cells = 0) */
#address-cells = <1>;
#size-cells = <0>;

eeprom@50 {
    reg = <0x50>;  /* I2C address 0x50 */
};
```

In driver code, you retrieve the base address with:

```c
struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
void __iomem *base = devm_ioremap_resource(&pdev->dev, res);
```

Or the modern shorthand:

```c
void __iomem *base = devm_platform_ioremap_resource(pdev, 0);
```

### 3.4 gpios / gpios-related properties

GPIOs are referenced using phandles pointing to a GPIO controller:

```json
/* GPIO controller (defined in bcm2711.dtsi) */
gpio: gpio@7e200000 {
    compatible = "brcm,bcm2711-gpio";
    reg = <0x7e200000 0xb4>;
    #gpio-cells = <2>;   /* <pin number> <flags> */
    gpio-controller;
};

/* A device using a GPIO */
leds {
    compatible = "gpio-leds";

    led_green: green {
        gpios = <&gpio 17 GPIO_ACTIVE_HIGH>;
        /*        ^phandle ^pin  ^flags     */
        default-state = "off";
    };
};
```

`GPIO_ACTIVE_HIGH` (0) and `GPIO_ACTIVE_LOW` (1) are defined in `<dt-bindings/gpio/gpio.h>`.
In a driver, reading a GPIO from the Device Tree:

```c
#include <linux/of_gpio.h>

int gpio_num = of_get_named_gpio(pdev->dev.of_node, "gpios", 0);
if (gpio_num < 0) {
    dev_err(&pdev->dev, "Failed to get GPIO\n");
    return gpio_num;
}

/* Or using the newer descriptor API */
#include <linux/gpio/consumer.h>

struct gpio_desc *gpiod = devm_gpiod_get(&pdev->dev, "led", GPIOD_OUT_LOW);
```

### 3.5 interrupts and interrupt-parent
Interrupt lines are described similarly to GPIOs:
```json
/* Interrupt controller */
intc: interrupt-controller@7e00b200 {
    compatible = "brcm,bcm2836-armctrl-ic";
    reg = <0x7e00b200 0x200>;
    interrupt-controller;
    #interrupt-cells = <2>;  /* <IRQ number> <type flags> */
};

uart0: serial@7e201000 {
    compatible = "brcm,bcm2835-pl011";
    reg = <0x7e201000 0x200>;
    interrupt-parent = <&intc>;
    interrupts = <2 25>;     /* bank 2, IRQ 25 */
};
```

In driver code:

```c
int irq = platform_get_irq(pdev, 0);
if (irq < 0)
    return irq;

ret = devm_request_irq(&pdev->dev, irq, my_irq_handler,
                       IRQF_SHARED, "my-device", dev);
```

### 3.6 `clocks`
Clock sources are described using phandles to a clock provider:

```json
clocks: clocks {
    compatible = "brcm,bcm2835-cprman";
    #clock-cells = <1>;      /* One cell selects the clock ID */
};

i2c1: i2c@7e804000 {
    compatible = "brcm,bcm2835-i2c";
    reg = <0x7e804000 0x1000>;
    clocks = <&clocks BCM2835_CLOCK_VPU>;
    clock-frequency = <100000>;   /* 100 kHz */
};
```

In driver code:

```c
#include <linux/clk.h>

struct clk *clk = devm_clk_get(&pdev->dev, NULL);
if (IS_ERR(clk))
    return PTR_ERR(clk);

ret = clk_prepare_enable(clk);
```

### 3.7 `#address-cells` and `#size-cells`
These meta-properties define the encoding of child reg properties. They are always specified on the parent node:

```json
soc {
    #address-cells = <1>;   /* 1 × 32-bit word = 32-bit address */
    #size-cells = <1>;      /* 1 × 32-bit word = 32-bit size */
    ranges;
    ...
};

/* On 64-bit systems, 2 cells = 64-bit address */
memory-controller@80000000 {
    #address-cells = <2>;
    #size-cells = <2>;
    ...
};
```

### 3.8 `ranges`
The ranges property creates an address translation mapping between child address space and parent address space:

```json
soc {
    compatible = "simple-bus";
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0x7e000000 0xfe000000 0x01800000>;
    /*        ^child addr ^parent addr ^length   */
    /* BCM2711: bus address 0x7e000000 → CPU address 0xfe000000 */
};
```

An empty `ranges`; means identity mapping (child address = parent address).

### 3.9 pinctrl / Pin Multiplexing
On Raspberry Pi, GPIO pins are multiplexed for different functions (UART, SPI, I2C, etc.). Pin configurations are described in the Device Tree:
```json
i2c1_pins: i2c1 {
    brcm,pins = <2 3>;               /* GPIO 2 and GPIO 3 */
    brcm,function = <BCM2835_FSEL_ALT0>; /* Alternate function 0 = I2C */
};

&i2c1 {
    pinctrl-names = "default";
    pinctrl-0 = <&i2c1_pins>;
    status = "okay";
};
```

### 3.10 Other Common Properties

Property | Description | Example |
|--------|-------------|---------|
| `model` | Human-readable board description | `"Raspberry Pi 4 Model B"` |
| `device_type` | Node class (`cpu`, `memory`) | `device_type = "memory";` | 
| `label` | Human-readable name for LEDs, GPIOs `label = "ACT";` | 
| `linux,default-trigger` | LED trigger | `"heartbeat", "mmc0"` | 
| `clock-frequency` | Bus/peripheral clock in Hz | `<100000>` (100 kHz) | 
| `bus-width` | Bus data width in bits | `<4>` (4-bit SDIO) | 
| `dr_mode` | USB controller mode | `"host"`, `"peripheral"`, `"otg"` | 
| `power-domains` | Power management domain | `<&pm BCM2835_POWER_DOMAIN_GRAFX>` |


## 4. Node Naming Conventions

```json
node-name@unit-address
```
* node-name - describes the device type (e.g., `uart`, `i2c`, `gpio`, `leds`)
* unit-address - hexadecimal base address from `reg`, without `0x` prefix
* If a node has no `reg` (no address), the `@unit-address` part is omitted

Standard generic names defined in the Device Tree Specification:

```
adc, cache-controller, clock, cpu, ethernet, gpio, i2c, interrupt-controller,
memory, mmc, pci, serial, sound, spi, timer, usb, watchdog
```

## 5. Phandles and References
Nodes can reference each other using labels that resolve to phandles (integer handles):

```json
/* Defining a label */
gpio: gpio@7e200000 { ... };

/* Referencing it with & */
device {
    gpios = <&gpio 17 0>;   /* &gpio resolves to gpio's phandle */
};
```

The `&label` syntax is also used to extend or override existing nodes (common in overlays and board files that include SoC `.dtsi` files):

```json
/* In bcm2711.dtsi */
uart0: serial@7e201000 {
    status = "disabled";   /* Off by default */
};

/* In bcm2711-rpi-4-b.dts */
&uart0 {
    status = "okay";       /* Board enables it */
};
```

## 6. Shell Commands for Exploring the Device Tree on Raspberry Pi

### 6.1 Locate Device Tree Files on the Pi
```bash
# Compiled .dtb files live in /boot/firmware (RPi OS Bookworm+)
ls /boot/firmware/*.dtb
ls /boot/firmware/overlays/*.dtbo

# On older RPi OS releases
ls /boot/*.dtb
ls /boot/overlays/*.dtbo
```

### 6.2 Decompile a `.dtb` Back to Human-Readable `.dts`

```bash
# Install device tree compiler
sudo apt install device-tree-compiler

# Decompile the running board's .dtb
dtc -I fs -O dts -s /sys/firmware/devicetree/base > rpi4b.dts

# View the result
less rpi4b.dts
```

### 6.3 Inspect the Live Device Tree via `/proc` and `/sys`
The kernel exposes the active Device Tree at `/proc/device-tree` (a sysfs-like interface):

```bash
# List all top-level DT nodes
ls /proc/device-tree/

# Read a string property
cat /proc/device-tree/model
# Output: Raspberry Pi 4 Model B

# Read the compatible property (null-separated strings)
cat /proc/device-tree/compatible | tr '\0' '\n'
# Output:
# raspberrypi,4-model-b
# brcm,bcm2711

# Navigate to a specific node
ls /proc/device-tree/soc/

# Read a binary/integer property using xxd
xxd /proc/device-tree/soc/gpio@7e200000/reg
```

### 6.4 Use dtc to Inspect the Live Flattened DT
```bash
# Read the live FDT passed by the bootloader
dtc -I fs /proc/device-tree 2>/dev/null | less

# Search for a specific compatible string
dtc -I fs /proc/device-tree 2>/dev/null | grep -A 5 "brcm,bcm2835-i2c"
```

### 6.5 Find Which DT Node Corresponds to a Device
```bash
# Each platform device exposed by DT appears under /sys/bus/platform/devices/
ls /sys/bus/platform/devices/ | grep fe20
# Output: fe200000.gpio  fe201000.serial  ...

# The of_node symlink points to the DT node in sysfs
ls -la /sys/bus/platform/devices/fe200000.gpio/of_node
# → ../../../../../../firmware/devicetree/base/soc/gpio@7e200000

# Read compatible directly from sysfs
cat /sys/bus/platform/devices/fe200000.gpio/of_node/compatible | tr '\0' '\n'
```

### 6.6 Check Which Driver is Bound to a DT Node
```bash
# View bound driver
ls -la /sys/bus/platform/devices/fe200000.gpio/driver
# → ../../../../bus/platform/drivers/pinctrl-bcm2711

# List all platform devices and their drivers
for dev in /sys/bus/platform/devices/*; do
    drv=$(readlink "$dev/driver" 2>/dev/null | xargs basename 2>/dev/null)
    echo "$(basename $dev) → ${drv:-no driver}"
done | sort
```

### 6.7 Load and Remove a Device Tree Overlay at Runtime
```bash
# List available overlays
ls /boot/firmware/overlays/

# Load an overlay at runtime using the configfs DT overlay interface
sudo su
mount -t configfs none /sys/kernel/config
mkdir /sys/kernel/config/device-tree/overlays/i2c-rtc
cat /boot/firmware/overlays/i2c-rtc.dtbo > \
    /sys/kernel/config/device-tree/overlays/i2c-rtc/dtbo

# Check if it loaded successfully
cat /sys/kernel/config/device-tree/overlays/i2c-rtc/status

# Remove the overlay
rmdir /sys/kernel/config/device-tree/overlays/i2c-rtc
```

### 6.8 Compile Your Own .dts File
```bash
# Write a minimal overlay
cat > ~/my_overlay.dts << 'EOF'
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&gpio>;
        __overlay__ {
            my_pins: my_pins {
                brcm,pins = <17>;
                brcm,function = <1>;   /* Output */
            };
        };
    };
};
EOF

# Compile to .dtbo
dtc -@ -I dts -O dtb -o ~/my_overlay.dtbo ~/my_overlay.dts

# Inspect the compiled blob
dtc -I dtb -O dts ~/my_overlay.dtbo
```

### 6.9 Find DT Bindings Documentation
```bash
# Kernel DT bindings are in Documentation/devicetree/bindings/
# On the Raspberry Pi, the kernel source may not be present, but you can browse online:
# https://www.kernel.org/doc/Documentation/devicetree/bindings/

# Locally, if kernel source is installed:
find /usr/src/linux-headers-$(uname -r) -name "*.yaml" -path "*/bindings/*" | head -20

# Or from the linux-doc package
sudo apt install linux-doc
ls /usr/share/doc/linux-doc/
```

## 7. How the Kernel Uses the Device Tree

```
Boot
 │
 ├─ Bootloader loads .dtb into memory, passes address in r2 (ARM) / x0 (ARM64)
 │
 ├─ setup_arch() → unflatten_device_tree()
 │       Parses DTB into kernel's internal tree structure (struct device_node)
 │
 ├─ of_platform_populate()
 │       Walks the DT, creates platform_device for each node with a compatible
 │       string that matches a registered of_device_id
 │
 └─ Driver's .probe() is called with platform_device
         Driver uses of_property_read_*() / devm_gpiod_get() / etc.
         to read properties from the DT node
```

In a driver, the `struct device_node` is accessed via:
```c
static int my_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    u32 frequency;
    const char *name;

    /* Read a u32 property */
    if (of_property_read_u32(np, "clock-frequency", &frequency))
        frequency = 100000;  /* default */

    /* Read a string property */
    if (of_property_read_string(np, "label", &name))
        name = "unknown";

    /* Check for a boolean property (presence = true) */
    if (of_property_read_bool(np, "big-endian"))
        flags |= MY_FLAG_BIG_ENDIAN;

    return 0;
}
```

## 8. Summary

Concept | Key Points
--------|-----------|
Why DT exists | Eliminates hard-coded board files; same kernel, different `.dtb`
`.dts` | Human-readable source; top-level board description
`.dtsi` | Reusable SoC/platform fragments, included by `.dts`
`.dtb` | Compiled binary passed to kernel by bootloader
`.dtbo` | Overlay binary for runtime or boot-time hardware modification
`compatible` | Drives driver matching; most specific → most generic
`status` | `"okay"` enables, `"disabled"` suppresses driver probe
`reg` |  Memory-mapped base address + size (format set by `#address-cells`/`#size-cells`)
`gpios` | Phandle reference to GPIO controller + pin number + flags
`interrupts` | Phandle reference to IRQ controller + IRQ number + type
`clocks` | Phandle reference to clock provider + clock ID
`ranges` | Address translation from child to parent bus address space
Phandles | Labels (`&name`) allow cross-node references and node extension

