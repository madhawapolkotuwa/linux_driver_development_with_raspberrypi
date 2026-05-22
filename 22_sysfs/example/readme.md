# What This Example Demonstrates

This is a single self-contained module that demonstrates all
sysfs mechanisms in one place:


Concept | What this example does |
--------|------------------------|
`name` Attribute | Readonly - Read back sensor name 
`value` Attribute | Read-Write - read the sensor value & reset the value 
Binary attributes | Exposes a 16-byte calibration blob readable and writable as raw binary 
Attribute groups | Creates a `config/` subdirectory holding mode and threshold attributes  
`sysfs_notify()` | A workqueue ticks every 5 seconds, updates value, and wakes any `poll()` waiting in user space

The driver simulates a fake sensor device. It registers as a platform driver,
creates a class interface under `/sys/class/mysensor/`, and builds the full
sysfs tree shown below.

```
/sys/class/mysensor/mysensor0/
├── name              ← DEVICE_ATTR_RO  - sensor name string
├── value             ← DEVICE_ATTR_RW  - integer, writable + sysfs_notify
├── calibration       ← BIN_ATTR        - 16-byte raw binary blob
└── config/           ← attribute_group - subdirectory
    ├── mode          ← DEVICE_ATTR_RW  - operating mode string
    └── threshold     ← DEVICE_ATTR_RW  - integer threshold
```

## Key Code Sections

### 1. Standard device attributes - `DEVICE_ATTR_RO` / `DEVICE_ATTR_RW`  

**name** is read-only. **value** is read-write and calls `sysfs_notify()` on
every write so user space `poll()` wakes up immediately.

```c
static DEVICE_ATTR_RO(name);    /* expands to DEVICE_ATTR(name, 0444, name_show, NULL)  */
static DEVICE_ATTR_RW(value);   /* expands to DEVICE_ATTR(value, 0644, value_show, value_store) */
```

Inside `value_store()`, after updating the value:
```c
sysfs_notify(&dev->kobj, NULL, "value");
```
This wakes any user space process that called `poll()` or `select()` on the
value file.

### 2. Attribute group - config/ subdirectory
Grouping attributes with a `.name` field creates a subdirectory automatically.
All attributes in the group are created and destroyed in a single call.

```c
static struct attribute *config_attrs[] = {
    &dev_attr_mode.attr,
    &dev_attr_threshold.attr,
    NULL,
};

static struct attribute_group config_group = {
    .name  = "config",   /* creates the config/ subdirectory */
    .attrs = config_attrs,
};

/* In probe - one call creates both files + the directory: */
sysfs_create_group(&sdev->class_dev->kobj, &config_group);

/* In remove - one call removes both files + the directory: */
sysfs_remove_group(&sdev->class_dev->kobj, &config_group);
```

### 3. Binary attribute - raw calibration blob
`BIN_ATTR` defines a binary attribute. The read and write callbacks
receive a raw buffer and an offset   
similar to `file_operations` but routed through **sysfs**.
```c
static BIN_ATTR(calibration, 0644, calib_read, calib_write, BIN_BLOB_SIZE);

/* In probe: */
device_create_bin_file(sdev->class_dev, &bin_attr_calibration);

/* In remove: */
device_remove_bin_file(sdev->class_dev, &bin_attr_calibration);
```

The read callback uses `kobj_to_dev()` to get back to the device, then
`dev_get_drvdata()` to reach the private sensor_dev struct.

### 4. Workqueue + sysfs_notify()
A delayed workqueue simulates a hardware sensor that changes its reading
every 5 seconds. Each tick increments value and calls `sysfs_notify()`
so user space is woken up without polling.

```c
static void sensor_work_fn(struct work_struct *work)
{
    struct sensor_dev *sdev =
        container_of(work, struct sensor_dev, dwork.work);

    sdev->value++;
    sysfs_notify(&sdev->class_dev->kobj, NULL, "value");

    schedule_delayed_work(&sdev->dwork, msecs_to_jiffies(5000));
}
```
In `remove()`, the workqueue is stopped before anything else:
```c
cancel_delayed_work_sync(&sdev->dwork);   /* must be first */
```

`cancel_delayed_work_sync()` waits for any currently running work to
finish before returning. This ensures no `sysfs_notify()` call happens
after the **sysfs** entries have been removed.

### 5. Probe error path

If any `device_create_*` call fails in `probe()`, the error path undoes
all previous calls in reverse order before returning the error.

```c
err_group:
    device_remove_group(sdev->class_dev, &config_group);
err_value:
    device_remove_file(sdev->class_dev, &dev_attr_value);
err_name:
    device_remove_file(sdev->class_dev, &dev_attr_name);
err_device:
    device_destroy(sdev->cls, MKDEV(0, 0));
err_class:
    class_destroy(sdev->cls);
    return ret;
```

This is the standard Linux kernel error-path pattern     
Always clean up what you already created before returning an error.

-----------
-----------
<br>

## Test


#### Build

```bash
make
```

#### Load the module
```
sudo insmod sysfs_ex.ko
```

`dmesg` output:
```
sysfs_ex sysfs_ex: probe called
sysfs_ex sysfs_ex: ready - sysfs entries created
sysfs_ex: module loaded
```
-----
#### Verify the sysfs tree
```bash
ls /sys/class/mysensor/mysensor0/
# name  value  calibration  config/  uevent  ...

ls /sys/class/mysensor/mysensor0/config/
# mode  threshold
```

----------

#### Read and write standard attributes
```bash
# Read the sensor name
cat /sys/class/mysensor/mysensor0/name

# Read the current value
cat /sys/class/mysensor/mysensor0/value

# Write a new value - triggers sysfs_notify immediately
echo "12" | sudo tee /sys/class/mysensor/mysensor0/value

# Read it back
cat /sys/class/mysensor/mysensor0/value
```
------

#### Read and write the config group
```bash
cat /sys/class/mysensor/mysensor0/config/mode

echo "manual" | sudo tee /sys/class/mysensor/mysensor0/config/mode

cat /sys/class/mysensor/mysensor0/config/threshold

echo "250" | sudo tee /sys/class/mysensor/mysensor0/config/threshold
```

--------------

#### Read and write the binary attribute
```bash
# switch to super user
sudo su
# Read the raw 16-byte calibration blob (hexdump it)
hexdump /sys/class/mysensor/mysensor0/calibration
# 00000000: abab abab abab abab abab abab abab abab  ................

# Write new calibration data (first 4 bytes)
printf '\x01\x02\x03\x04' | dd of=/sys/class/mysensor/mysensor0/calibration bs=1

# Verify
hexdump /sys/class/mysensor/mysensor0/calibration
# 00000000: 0102 0304 abab abab abab abab abab abab  ................

# exit super user
exit
```
-------
-------

### Demonstrate `sysfs_notify()` with the poll script

In Terminal 3, start the poll script:    
```bash
python3 poll_sensor.py
```

Output:
```
Opening /sys/class/mysensor/mysensor0/value
Waiting for sysfs_notify() from the driver (Ctrl+C to stop)...

Initial value: 12
```

Now wait. Every 5 seconds the workqueue fires, increments value, and calls
`sysfs_notify()`. The script wakes up and prints the new value without
any busy-loop:

```
[10:23:05] sysfs_notify received value: 13
[10:23:10] sysfs_notify received value: 14
[10:23:15] sysfs_notify received value: 15
```
You can also trigger a notification manually from another terminal:
```bash
echo "99" | sudo tee /sys/class/mysensor/mysensor0/value
```

The poll script wakes up immediately:
```
[10:23:17] sysfs_notify received value: 99
```

--------

### Unload the module
```bash
sudo rmmod sysfs_ex
```
The entire `/sys/class/mysensor/` tree is gone. The poll script will get
an error on the next read and exit cleanly.

After loading:
```
/sys/class/mysensor/
└── mysensor0/
    ├── name           (0444) cat → "mysensor0"
    ├── value          (0644) cat → integer · echo → update + notify
    ├── calibration    (0644) xxd → raw bytes · dd → write raw bytes
    └── config/
        ├── mode       (0644) cat/echo → operating mode string
        └── threshold  (0644) cat/echo → integer threshold
```


