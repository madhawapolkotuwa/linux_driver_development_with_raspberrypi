obj-m += my_cdev.o

all: module testapp

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm a.out

testapp: test1.c
	gcc test1.c