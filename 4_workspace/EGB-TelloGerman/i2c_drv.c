#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("julianvroey (TD3 UTN FRA)");
MODULE_DESCRIPTION("Device driver I2C para comunicarse con la BluePill");
MODULE_VERSION("1.0");

#define drvNAME "td3_i2c"
#define drvCLASS "td3_class"

#define COMPATIBLE "td3_i2c"
#define defBUFFER_SIZE 128

#define procFS_FILE "i2c"
#define procFS_FOLDER "td3"

//estructura del procf: defin el procf donde se guardaran los datos que se transmita por i2c
typedef struct {
        struct proc_dir_entry *file, *folder;
} mypfs_t;

//estructura de comunicacion i2c: contiene las variables que definen la comunicacion i2c
typedef struct {
	struct i2c_client *client;
	int bsize;
	mypfs_t pfs;
} myi2c_t;

//estrictura del driver
typedef struct {
	dev_t number;
        struct class *class;
        struct cdev cdev;
} mydrv_t;

//estructura del dispositivo i2c: contiene todas las variables del dispositivo
typedef struct {
	mydrv_t drv;
	myi2c_t i2c;
} mydev_t;
static mydev_t mydev;

static int i2cProbe(struct i2c_client *client);
static void i2cRemove(struct i2c_client *client);

static int drvOpen(struct inode *device_file, struct file *instance);
static int drvClose(struct inode *device_file, struct file *instance);
static ssize_t drvRead(struct file *File, char *user_buffer, size_t count, loff_t *offs);
static ssize_t drvWrite(struct file *File, const char *user_buffer, size_t count, loff_t *offs);
static long int drvIoctl(struct file *file, unsigned cmd, unsigned long arg);

static ssize_t i2cWrite(struct file *File, const char *user_buffer, size_t count, loff_t *offs);
static ssize_t i2cRead(struct file *File, char *user_buffer, size_t count, loff_t *offs);

static struct of_device_id i2c_drv_ids[] = {
	{.compatible = COMPATIBLE},
	{},
};
MODULE_DEVICE_TABLE(of, i2c_drv_ids);

static struct i2c_device_id i2c_dev[] = {
	{"i2c_dev", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, i2c_dev);

static struct i2c_driver td3_i2c_drv = {
	.probe = i2cProbe,
	.remove = i2cRemove,
	.id_table = i2c_dev,
	.driver = {
		.name = COMPATIBLE,
		.of_match_table = i2c_drv_ids
	}
};

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = drvOpen,
	.release = drvClose,
	.read = drvRead,
	.write = drvWrite,
	.unlocked_ioctl = drvIoctl
};

static struct proc_ops i2cFops = {
        .proc_write = i2cWrite,
        .proc_read = i2cRead
};

static int __init moduleInit(void) {
	pr_info("TD3 - Insertando el modulo de kernel");
	if(i2c_add_driver(&td3_i2c_drv)) {
		pr_err("TD3 - Error creando driver de dispositivo i2c\n");
		return -1;
	}
	if(0 > alloc_chrdev_region(&mydev.drv.number, 0, 1, drvNAME)) {
		pr_err("TD3 - Error, no se pudo alocar el numero de driver\n");
		goto init_err;
	}
	pr_info("TD3 - Driver registrado con numeros major=%d y minor=%d\n", mydev.drv.number >> 20, mydev.drv.number & 0xfffff);
	if(NULL == (mydev.drv.class = class_create(drvCLASS))) {
		pr_err("TD3 - Error, no se pudo crear la clase de dispositivo\n");
		goto init_err1;
	}
	if(NULL == device_create(mydev.drv.class, NULL, mydev.drv.number, NULL, drvNAME)) {
		pr_err("TD3 - Error creando el archivo de driver\n");
		goto init_err2;
	}
	cdev_init(&mydev.drv.cdev, &fops);
	if(-1 == cdev_add(&mydev.drv.cdev, mydev.drv.number, 1)) {
		pr_err("Td3 - Error, no se pudo registrar el driver en el kernel\n");
		goto init_err3;
	}
	pr_info("TD3 - Modulo insertado");
	return 0;
init_err3:
	device_destroy(mydev.drv.class, mydev.drv.number);
init_err2:
	class_destroy(mydev.drv.class);
init_err1:
	unregister_chrdev_region(mydev.drv.number, 1);
init_err:
	i2c_del_driver(&td3_i2c_drv);
	return -1;
} module_init(moduleInit);

static void __exit moduleExit(void) {
	pr_info("TD3 - Removiendo el modulo de kernel");
	cdev_del(&mydev.drv.cdev);
	device_destroy(mydev.drv.class, mydev.drv.number);
	class_destroy(mydev.drv.class);
	unregister_chrdev_region(mydev.drv.number, 1);
	i2c_del_driver(&td3_i2c_drv);
} module_exit(moduleExit);

//_____________________________________________________________________

static ssize_t i2cWrite(struct file *File, const char *user_buffer, size_t count, loff_t *offs) {
	pr_info("TD3 - i2cWrite");
	char data[defBUFFER_SIZE];
	int to_copy, not_copied;
	memset(data, 0, sizeof(data));
	to_copy = min(count, sizeof(data));
	pr_info("TD3 - escribiendo por i2c");
	not_copied = copy_from_user(data, user_buffer, to_copy);
	i2c_master_send(mydev.i2c.client, data, mydev.i2c.bsize);
	pr_info("TD3 - se envio %s por i2c", data);
	pr_info("x");
	return to_copy - not_copied;
}

static ssize_t i2cRead(struct file *File, char *user_buffer, size_t count, loff_t *offs) {
	pr_info("TD3 - i2cRead");
	char data[defBUFFER_SIZE];
 	int to_copy, not_copied;
	memset(data, 0, sizeof(data));
   	to_copy = min(count, sizeof(data));
   	i2c_master_recv(mydev.i2c.client, data, mydev.i2c.bsize);
	not_copied = copy_to_user(user_buffer, data, to_copy);
	*offs += to_copy;
	pr_info("TD3 - se recibio %s por i2c", data);
	pr_info("x");
	return to_copy - not_copied;
}

static int i2cProbe(struct i2c_client *client) {
	mydev.i2c.client = client;
	pr_info("TD3 - i2cProbe\n");
	if(!device_property_present(&(mydev.i2c.client)->dev, "buffer_size")) {
		pr_err("TD3 - No se encuentra la propiedad buffer_size\n");
		return -1;
	}
	if(device_property_read_u32(&(mydev.i2c.client)->dev, "buffer_size", &mydev.i2c.bsize)) {
		pr_err("TD3 - No se puede obtener buffer_size\n");
		return -1;
	}
	if(1 > mydev.i2c.bsize)
		mydev.i2c.bsize = defBUFFER_SIZE;
	if(NULL == (mydev.i2c.pfs.folder = proc_mkdir(procFS_FOLDER, NULL))) {
                pr_err("TD3 - Error creando /proc/td3\n");
                goto i2c_probe_err;
        }
	if(NULL == (mydev.i2c.pfs.file = proc_create(procFS_FILE, 0666, mydev.i2c.pfs.folder, &i2cFops))) {
		pr_err("TD3 - Error creando /proc/td3/i2c\n");
		goto i2c_probe_err1;
	}
	pr_info("TD3 - i2cprove creado");
	pr_info("X");
	return 0;
i2c_probe_err1:
	proc_remove(mydev.i2c.pfs.folder);
i2c_probe_err:
	return -1;
}

//_________________________________________________________________________

static void i2cRemove(struct i2c_client *client) {
	pr_info("TD3 - i2cRemove\n");
	proc_remove(mydev.i2c.pfs.file);
	proc_remove(mydev.i2c.pfs.folder);
}

static int drvOpen(struct inode *device_file, struct file *instance) {
	pr_info("TD3 - driverOpen\n");
	return 0;
}

static int drvClose(struct inode *device_file, struct file *instance) {
        pr_info("TD3 - driverRelease\n");
        return 0;
}

static ssize_t drvRead(struct file *File, char *user_buffer, size_t count, loff_t *offs) {
	int to_copy, not_copied;
	char data[defBUFFER_SIZE];
    // if (*offs > 0) return 0;
	memset(data, 0, sizeof(data));
	to_copy = min(count, sizeof(data));
	i2c_master_recv(mydev.i2c.client, data, mydev.i2c.bsize);
	not_copied = copy_to_user(user_buffer, data, to_copy);
	return to_copy - not_copied;
}

static ssize_t drvWrite(struct file *File, const char *user_buffer, size_t count, loff_t *offs) {
	int to_copy, not_copied;
        char data[defBUFFER_SIZE];
	memset(data, 0, sizeof(data));
	to_copy = min(count, sizeof(data));
	not_copied = copy_from_user(data, user_buffer, to_copy);
        i2c_master_send(mydev.i2c.client, data, mydev.i2c.bsize);
        return to_copy - not_copied;
}

static long int drvIoctl(struct file *file, unsigned cmd, unsigned long arg) {
	pr_info("TD3 - driverIoctrl\n");
	return 0;
}
