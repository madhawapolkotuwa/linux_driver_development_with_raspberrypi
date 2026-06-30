# Device Tree - GPIO Example

## Video :-

[![Youtube Video](https://img.youtube.com/vi/Klkup3wPgI8/0.jpg)](https://www.youtube.com/watch?v=Klkup3wPgI8)

In the [previous video](../25_device_tree_overlay/), we learned how to write, compile, and load Device Tree Overlays, and how to read different DT property types from a platform driver using the `device_property_read_*` API family.

In this video, we take a practical step forward,     
we define `GPIO pins` directly in the **Device Tree**. We will create a DT overlay that describes an LED and a button node, bind it to a platform driver, and use the GPIO descriptor API (`gpiod_*`) to request and control those GPIOs from within the driver. This is the recommended, modern approach for GPIO handling in Linux device drivers.


### Why Define GPIOs in the Device Tree?

Hardcoding GPIO numbers in driver source code is fragile,    
GPIO numbering can differ between boards and kernel versions.   
The Device Tree solves this by describing hardware connections declaratively, separate from driver logic.    
The driver simply asks for a named GPIO property, and the kernel resolves the actual pin.

### GPIO Descriptor API (`gpiod_*`)

The modern approach in Linux is the GPIO descriptor API, which replaces the legacy `gpio_request()` / `gpio_direction_output()` style. With the descriptor API:

* GPIOs are requested by name (matching the DT property name, minus the -gpios suffix)
* The descriptor carries direction, active-low polarity, and other metadata
* It is safer, more portable, and better integrated with the Device Tree

