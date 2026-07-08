# BMP180 I2C Sensor with a Device Tree Overlay

## Video :-

[![Youtube Video](https://img.youtube.com/vi/6cUBXAYrzRg/0.jpg)](https://www.youtube.com/watch?v=6cUBXAYrzRg)

This tutorial takes the BMP180 I2C driver from an [earlier video](../15_bmp180_i2c_sensor/sensor_read/)      
and moves the device instantiation out of the driver and into a Device Tree overlay,     
the same way we did for the GPIO example in [26_dt_gpio](../26_dt_gpio/)     
just this time for an I2C sensor instead of a GPIO line.

## What changed from the non-DT version

| Old approach (previous BMP180 video) | New approach (this tutorial) |
|---|---|
| `i2c_get_adapter()` + `i2c_new_client_device()` called from `module_init` | Device node declared in `bmp180_overlay.dts`, kernel creates the client automatically |
| Matching done only through `i2c_device_id` table (`"bmp180"`) | Matching done through `of_device_id` table with `compatible = "bmp180-sensor,myi2c"` |
| I2C address (`0x77`) hardcoded in `i2c_board_info` | I2C address set in the DT node's `reg` property |
| No `of_match_table` in the driver struct | `.of_match_table = bmp180_of_match` added to `i2c_driver` |
| `bmp180_ids[]` also carried a `meta_data` pointer for demonstration | Removed - kept a plain legacy `i2c_device_id` table for fallback only |

## Files in this tutorial

- `bmp180_i2c_dt_driver.c` - the updated I2C driver, matched via Device Tree
- `bmp180_overlay.dts` - the overlay describing the BMP180 node on `i2c1`

## Code walkthrough

**`bmp180_of_match[]`** - the Device Tree match table. The `compatible`
string here, `bmp180-sensor,myi2c`, must be identical to the `compatible`
property inside the overlay's `bmp180@77` node. This is the string the I2C
core uses to decide which driver's `probe()` to call.

**`.of_match_table = bmp180_of_match`** - this is what's added to the
`i2c_driver` struct so the driver actually participates in Device Tree
matching, instead of relying only on the legacy `id_table`.

**`bmp180_probe()`** - largely unchanged from the previous version. The only
addition is a log line printing `client->dev.of_node` with `%pOF`, so you can
confirm in `dmesg` that the device really was matched through the DT node and
not through the legacy ID table.

**`bmp180_init()` / `bmp180_exit()`** - much simpler now. There's no more
`i2c_get_adapter()`, no `i2c_board_info`, and no `i2c_new_client_device()` /
`i2c_unregister_device()` pair. The module just registers and unregisters the
driver; the overlay is responsible for creating and destroying the device
node.

## Building and testing

Build the module the same way as previous tutorials:

```bash
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

Compile the overlay:

```bash
dtc -@ -I dts -O dtb -o bmp180_overlay.dtbo bmp180_overlay.dts
```

Or use `Makefile`
```bash
# build
make 

# load
make load

# unload
make unload
```

Install both:

```bash
sudo cp bmp180_overlay.dtbo /boot/firmware/overlays/
echo "dtoverlay=bmp180_overlay" | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

After rebooting, load the driver:

```bash
sudo insmod bmp180_i2c_dt_driver.ko
```

Read the sensor:

```bash
cat /sys/bus/i2c/devices/1-0077/temp_input
cat /sys/bus/i2c/devices/1-0077/pressure_input
```

Expected output (values will vary with real conditions):

```
215
101325
```

`temp_input` is in tenths of a degree Celsius (215 = 21.5 C), and
`pressure_input` is in Pascals.

Unload:

```bash
sudo rmmod bmp180_i2c_dt_driver
```

## Summary

| Item | Value |
|---|---|
| Compatible string | `bmp180-sensor,myi2c` |
| I2C bus | `i2c1` |
| I2C address | `0x77` |
| sysfs attributes | `temp_input`, `pressure_input` |
| Driver name | `bmp180-i2c-driver` |



