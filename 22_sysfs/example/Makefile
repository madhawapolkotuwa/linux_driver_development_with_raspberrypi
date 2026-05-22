obj-m += sysfs_ex.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean


load: 
	@echo "--- Loading sysfs_ex ---"
	sudo insmod sysfs_ex.ko

unload: 
	@echo "--- Unloading sysfs_ex ---"
	sudo rmmod sysfs_ex.ko