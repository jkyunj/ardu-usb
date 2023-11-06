obj-m += ardu_usb.o

CONFIG_MODULE_SIG=n
KDIR=/lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: ardu_usb.c
	make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
