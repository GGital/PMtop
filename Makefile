# procmon - top-level build
#
#   make            build kernel module + TUI
#   make kmod       build only kmod/procmon.ko
#   make tui        build only tui/pmtop
#   make load       insmod the module      (needs sudo)
#   make unload     rmmod the module       (needs sudo)
#   make run        start the TUI
#   make test       TUI unit tests (+ procfs checks if the module is loaded)
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

test:
	$(MAKE) -C tui test
	@if [ -d /proc/procmon ]; then ./scripts/smoke_test.sh && ./scripts/events_test.py && ./scripts/xcheck_proc.py; \
	else echo "(module not loaded: skipping procfs checks)"; fi

clean:
	$(MAKE) -C kmod clean
	$(MAKE) -C tui clean

.PHONY: all kmod tui load unload run test clean
