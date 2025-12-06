## Kernel Log levels

* Link to Linux Device Driver 3rd Edition book: [ldd3](https://lwn.net/Kernel/LDD3/)

* Link to the Log levels reference: [log levels](https://docs.kernel.org/core-api/printk-basics.html)


### shell commands used in this tutorial:

* copy files inside the `01_hello` folder to new `02_log_levels` folder:
```
cp -r 01_hello/ 02_log_levels
```

* rename `hello.c` to `log_levels.c`:
```
mv hello.c log_levels.c
```

* edith the `Makefile` with nano editor:
```
nano Makefile
```

* load the `log_module.ko`
```
sudo insmod log_levels.ko
```

* remove `log_levels`
```
sudo rmmod log_levels
```

* get kernel message buffer
```
dmesg
```

* live update of the k messages
```
dmesg -W
```

* only get 0th number level messages
```
dmesg --level=0
```

* get 3,4 number levels messages
```
dmesg --level=3,4
```