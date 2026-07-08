obj-m += bmp180_i2c_dt_driver.o
bmp180_i2c_dt_driver-objs := src/bmp180_i2c_dt_driver.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o dts/bmp180_overlay.dtbo dts/bmp180_overlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f dts/*.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d ./dts bmp180_overlay
	@echo "load the the kernel module..."
	sudo insmod bmp180_i2c_dt_driver.ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod bmp180_i2c_dt_driver.ko
	@echo "remove DT overlay..."
	sudo dtoverlay -r bmp180_overlay

