obj-m += mybus.o
obj-m += mydevice.o
obj-m += mydriver.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean


load: 
	@echo "--- Loading mybus ---"
	sudo insmod mybus.ko
	@echo "--- Loading mydevice ---"
	sudo insmod mydevice.ko
	@echo "--- Loading mydriver ---"
	sudo insmod mydriver.ko

unload: 
	@echo "--- Unloading mydriver ---"
	sudo rmmod mydriver.ko
	@echo "--- Unloading mydevice ---"
	sudo rmmod mydevice.ko
	@echo "--- Unloading mybus ---"
	sudo rmmod mybus.ko