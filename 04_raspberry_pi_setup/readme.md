## Linux Kernel Programming on a Raspberry Pi

* Udate Packages with: 
```
sudo apt update && sudo apt upgrade -y
```
* Install Kernel Headers: 
```
sudo apt install -y raspberrypi-kernel-headers
```
* Install build tools, like gcc, make, ...: 
```
sudo apt install -y build-essential
```
* Reboot to load new kernel: 
```
sudo reboot
```


* Check kernel version
```
uname -r
```

* copy files from old directory(old_dir) and create new directory(new_dir) and paste files from (old_dir)
```
cp -r old_dir new_dir
```

