project_name := of_parse_demo

obj-m += $(project_name).o
$(project_name)-objs := src/$(project_name).o

all: module dtbo

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dtbo:
	dtc -@ -I dts -O dtb -o dts/$(project_name).dtbo dts/$(project_name).dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f dts/*.dtbo

load:
	@echo "Activating DT overlay..."
	sudo dtoverlay -d ./dts $(project_name)
	@echo "load the the kernel module..."
	sudo insmod $(project_name).ko

unload:
	@echo "Unload the kernel module..."
	sudo rmmod $(project_name).ko
	@echo "remove DT overlay..."
	sudo dtoverlay -r $(project_name)

