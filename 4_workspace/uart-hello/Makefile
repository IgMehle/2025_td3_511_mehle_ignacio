obj-m += uart_hello_serdev.o

KDIR := /lib/modules/$(shell uname -r)/build
MDIR := $(PWD)/build

DEV_TREE := uart_hello
FILE := uart_hello_serdev

all:
	@mkdir -p build;					\
	cp -f ${FILE}.c Makefile ${MDIR};	\
	cd build;							\
	make -C ${KDIR} M=${MDIR} modules

clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build

build-devtree:
	@dtc -@ -I dts -O dtb -o build/$(DEV_TREE).dtbo $(DEV_TREE).dts

load-devtree:
	@sudo cp build/${DEV_TREE}.dtbo /boot/overlays/

unload-devtree:
	@sudo dtoverlay -r ${DEV_TREE}

insmod:
	@sudo insmod build/${FILE}.ko

rmmod:
	@sudo rmmod  ${FILE}
