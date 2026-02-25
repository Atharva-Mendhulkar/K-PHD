obj-m += kphd.o

# The KERNELDIR is where we look for the kernel headers.
# By default we look at the headers of the currently running kernel.
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
