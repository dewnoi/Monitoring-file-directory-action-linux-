obj-m += monitor_kmod.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc monitor.c -o monitor

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm monitor