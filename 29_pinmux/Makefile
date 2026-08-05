obj-m += pinctrl_demo.o
pinctrl_demo-objs := src/pinctrl_demo.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o dts/pinctrl_demo_overlay.dtbo dts/pinctrl_demo_overlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f dts/*.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d ./dts pinctrl_demo_overlay
	@echo "load the the kernel module..."
	sudo insmod pinctrl_demo.ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod pinctrl_demo.ko
	@echo "remove DT overlay..."
	sudo dtoverlay -r pinctrl_demo_overlay

