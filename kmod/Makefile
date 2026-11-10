# Out-of-tree kernel module build for procmon.ko
#
#   make            build against the running kernel
#   make KDIR=...   build against another kernel tree
#   make clean

obj-m       := procmon.o
procmon-y   := procmon_main.o pm_collect.o pm_procfs.o pm_events.o
ccflags-y   := -I$(src)/../include -Wall

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
