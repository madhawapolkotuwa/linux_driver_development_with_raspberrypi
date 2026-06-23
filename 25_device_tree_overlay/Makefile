obj-m += dt_probe.o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o testoverlay.dtbo testoverlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d . testoverlay
	@echo "load the the kernel module..."
	sudo inmsmod dt_probe.ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod dt_probe
	@echo "remove DT overlay..."
	sudo dtoverlay -r testoverlay