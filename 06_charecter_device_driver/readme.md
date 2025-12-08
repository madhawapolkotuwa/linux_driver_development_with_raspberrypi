# Understanding Linux Device Files

## Block and Charecter Devices

## Major & Minor 

### Where do device files live and why are they “virtual”?

* The `/dev` directory contains special files called device nodes.
```
ls /dev
ls /dev | wc -l
```


* b = block, c = character
```
ls -lh /dev/mmcblk0 /dev/tty0 /dev/gpiochip0
```

* minor number
```
ls -lh /dev/tty{1,2,3,4,5}
```

* get registerd major numbers
```
cat /proc/devices
```

### filename doesn’t matter

**Character Device: Serial Port example**
* Enable Serial port
```
sudo raspi-config
```
* install screen
```
sudo apt install screen -y
```

* screen serial port (short the UART TX and RX pins)
```
sudo screen /dev/ttyS0 9600
```
*Whatever we type anyting it will echo back, showing that the serial interface works.*   
<small>exit form screen: `Ctrl + A` , Then, `press D`.</small>

* Create `my_serial` device node
```
sudo mknod my_serial c 4 64
```
* again screen serail bus using new node `my_serial`:
```
sudo screen my_serial 9600
```
<small>This works exact same. like `ttyS0`</small>

**The filename doesn’t matter – only major+minor do.**

**Block Device: SD Card Memeory block example**
* dump the `mmcblk0` 4kb memory,
```
sudo hexdump -C /dev/mmcblk0 -n 512 | head
```

* create our own device node:
```shell
sudo mknod my_mblk b 179 0 # 179,0 is the same major and minor number of the mmcblk0
```
```
ls -lh my_mblk
```
* Read 1st 4kb of data of new device node `my_mblk`
```
sudo hexdump -C my_mblk -n 512 | head
```
