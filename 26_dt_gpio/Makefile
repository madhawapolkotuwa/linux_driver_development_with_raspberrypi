obj-m += gpio_dt_driver.o
gpio_dt_driver-objs := src/gpio_dt_driver.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o dts/my_gpio_overlay.dtbo dts/my_gpio_overlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f dts/*.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d ./dts my_gpio_overlay
	@echo "load the the kernel module..."
	sudo insmod gpio_dt_driver.ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod gpio_dt_driver.ko
	@echo "remove DT overlay..."
	sudo dtoverlay -r my_gpio_overlay

