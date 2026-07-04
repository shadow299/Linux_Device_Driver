#kbuild part of makefile
obj-m := c_driver.o

#normal makefile content
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all default : modules

modules help clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(shell pwd) $@