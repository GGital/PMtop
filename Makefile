# pmon - process monitor kernel module + pmview terminal viewer
#
#   make          build module/pmon.ko and viewer/pmview
#   make load     insmod the module      (asks for sudo)
#   make unload   rmmod the module       (asks for sudo)
#   make run      start the viewer
#   make demo     build, load, test and unload the module (./demo.sh)
#   make clean

all: module viewer

module:
	$(MAKE) -C module

viewer:
	$(MAKE) -C viewer

load: module
	-lsmod | grep -q '^pmon ' && sudo rmmod pmon
	sudo insmod module/pmon.ko
	@sudo dmesg | grep 'pmon v' | tail -1

unload:
	sudo rmmod pmon

run: viewer
	./viewer/pmview

demo:
	./demo.sh

clean:
	$(MAKE) -C module clean
	$(MAKE) -C viewer clean

.PHONY: all module viewer load unload run demo clean
