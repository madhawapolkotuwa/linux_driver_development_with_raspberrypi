# OF (Open Firmware) Parsing

## Video :-

[![Youtube Video](https://img.youtube.com/vi/K1qPtZPCLPY/0.jpg)](https://www.youtube.com/watch?v=K1qPtZPCLPY)

Every tutorial so far has used Device Tree data 
- GPIO number
- intigers
- strings
- arrays, etc

without stopping to look at the API layer that actually pulls that data out of the DT node and into our driver. This episode is that layer.

Here we build a small *platform driver*, `of_parse_demo`, whose entire job is to read one property of each common type,
- string
- integer
- integer array
- boolean flag
- GPIO   

out of its own DT node and print what it found. using direct **DT parsing**.

## Why "OF"?

The Linux Device Tree API is still prefixed `of_` throughout the kernel (`of_property_read_u32`, `of_get_named_gpio`, `of_find_node_by_name`, and so on).    
This is a historical holdover: Device Tree itself originated in the **Open Firmware (OF)** standard used by *Sun* and *PowerPC* systems long before it was adopted for *ARM* and *RISC-V* embedded boards.   
The name stuck even though "`Open Firmware`" as a boot standard is largely irrelevant to how we use Device Tree today.   
When we see `of_*`, read it as "`Device Tree API`".

## The parsing helper family

Every property in a `.dts` node is stored internally as a **raw byte blob** attached to a `struct device_node`.      
The `of_*` helpers exist so our driver never has to interpret those bytes manually,     
we tell the helper what type we expect, and it decodes and validates for us.

Property in DT |Helper API |Notes
---------------|---------------|-----------
**String**|`of_property_read_string()`|Returns a pointer into DT data, no copy needed
**Single integer**|`of_property_read_u32()` (also `_u8`, `_u16`, `_u64`)|Fails with `-EINVAL` if the property is missing
**Integer array**|`of_property_count_u32_elems()` + `of_property_read_u32_array()`|Always check the count before reading, array sizes vary per board
**Presence-only boolean**|`of_property_read_bool()`|True if the property exists at all, regardless of value, DT booleans carry no data
**GPIO**|`of_get_named_gpio()`|Legacy OF-level API, returns a raw GPIO number, not a `struct gpio_desc`


### A note on the GPIO API choice

In the last tutorials we used the modern `gpiod` / `pinctrl` consumer API (`devm_gpiod_get()`) for controlling pins.     
Here we deliberately uses the older, **lower-level** `of_get_named_gpio()` instead, because the topic here is **DT parsing** itself,    
not GPIO consumption. `of_get_named_gpio()` returns a plain integer GPIO number straight out of the **DT phandle + specifier**, which is exactly what "*parsing a GPIO from DT*" looks like at the **OF** layer.     
In new driver code we'd normally reach for `devm_gpiod_get()`, which wraps this same OF parsing internally and hands a `struct gpio_desc *` instead of a bare number, but seeing the raw call here makes clear what's happening underneath.

* The demo node:
    ```
    of_parse_demo: of-parse-demo {
        compatible = "mpcoding,of-parse-demo";
        status = "okay";

        mpcoding,device-name = "sensor-alpha";
        mpcoding,sample-rate-hz = <100>;
        mpcoding,thresholds = <10 20 30>;
        mpcoding,enable-feature;
        reset-gpios = <&gpio 27 1>;
    };
    ```

    Each line maps directly to one row in the API table above. The `mpcoding`,   
    prefix marks these as vendor-specific properties (not part of any standard binding),     
    which is exactly how we'd namespace properties for a real out-of-tree driver.

* The demo driver

    `of_parse_demo_parse_dt()` reads all five properties in `probe()`, in the same order they appear in the overlay, logging every value with `dev_info()` so it's visible in dmesg. The GPIO is then requested with `devm_gpio_request_one()` and pulsed high-then-low for 10 milliseconds, just to prove the number that came out of `of_get_named_gpio()` is a real, usable GPIO and not just an integer we printed and ignored.

    If any property is missing or malformed, `probe()` fails immediately with a descriptive `dev_err()` this is intentional, so you can see exactly what a parsing failure looks like in dmesg. Try deleting a property from the overlay and reloading to see it for yourself.
    
        






