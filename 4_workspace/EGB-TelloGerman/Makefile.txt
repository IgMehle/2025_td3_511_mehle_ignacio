# Etiqueta para definir el nombre del archivo del modulo
FILE := i2c_drv
# Nombre del device tree overlay
DEV_TREE := i2c_dt
# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build
# Filtro para dmesg
AUTHOR := TD3
# Nombre de editor de texto
EDITOR := nano
# Etiqueta para definir el nombre del programa de usuario
USER := mainuser81


# Codigo fuente para compilar
obj-m += $(FILE).o

# Compilacion completa
all: clean build dtbuild

# Compilacion
build:
	@mkdir -p build;	\
	cp -f ${FILE}.c Makefile ${MDIR};	\
	cd build;
	make -C ${KDIR} M=${MDIR} modules

# Compilacion del device tree overlay
dtbuild:
	dtc -@ -I dts -O dtb -o build/${DEV_TREE}.dtbo ${DEV_TREE}.dts

# Limpieza de archivos de compilacion
clean:
	rm -rf ${MDIR}

# Cargo el modulo
insmod:
	sudo insmod build/${FILE}.ko

# Elimino el modulo
rmmod:
	sudo rmmod ${FILE}.ko

# Cargo el device tree overlay
dtoverlay:
	sudo dtoverlay build/${DEV_TREE}.dtbo

# Filtra mensajes del kernel
dmesg:
	dmesg | grep ${AUTHOR}

# Cargo modulo y device tree overlay
dtinsmod:
	sudo dtoverlay build/${DEV_TREE}.dtbo
	sudo insmod build/${FILE}.ko

# Elimino modulo y device tree overlay
dtrmmod:
	sudo rmmod ${FILE}.ko
	sudo dtoverlay -r ${DEV_TREE}

# Edicion del codigo
edit:
	${EDITOR} ${FILE}.c

# Compila el programa de usuario
user:
	@mkdir -p build;					\
	gcc -o ${MDIR}/${USER} ${USER}.c
