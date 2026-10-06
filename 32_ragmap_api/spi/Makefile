project_name := regmap_demo

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
	@echo "Installing module into modules tree..."
	sudo mkdir -p /lib/modules/$(shell uname -r)/extra
	sudo cp $(project_name).ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	@echo "Loading regmap-spi dependency and module..." 
	sudo modprobe regmap-spi
	sudo modprobe $(project_name)

unload:
	@echo "Unload the kernel module..."
	sudo modprobe -r $(project_name)
	@echo "remove DT overlay..."
	sudo dtoverlay -r $(project_name)
	@echo "remove copy of $(project_name).ko"
	find /lib/modules/$(uname -r) -name "$(project_name).ko*"
	sudo rm -f /lib/modules/$(uname -r)/extra/$(project_name).ko
	sudo depmod -a


