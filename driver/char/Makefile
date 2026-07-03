# Out-of-tree kernel module build.
#
# Usage (在 Guest 内执行):
#   cd ~/miniaccel-driver
#   make
#   sudo insmod miniaccel_drv.ko
#   sudo rmmod  miniaccel_drv
#   # EDU fallback:
#   sudo insmod miniaccel_edu_drv.ko
#   sudo rmmod  miniaccel_edu_drv
#   make clean

ccflags-y += -I$(src)/../../include/uapi
obj-m := miniaccel_drv.o miniaccel_edu_drv.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean

.PHONY: all clean
