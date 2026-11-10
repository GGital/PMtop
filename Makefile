# procmon - top-level build
#
#   make            build kernel module + TUI
#   make kmod       build only kmod/procmon.ko
#   make tui        build only tui/pmtop
#   make load       insmod the module      (needs sudo)
#   make unload     rmmod the module       (needs sudo)
#   make run        start the TUI
#   make clean

all: kmod tui

kmod:
	$(MAKE) -C kmod

tui:
	$(MAKE) -C tui

load: kmod
	./scripts/load.sh

unload:
	./scripts/unload.sh

run: tui
	./tui/pmtop

clean:
	$(MAKE) -C kmod clean
	$(MAKE) -C tui clean

.PHONY: all kmod tui load unload run clean
