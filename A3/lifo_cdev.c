/*
 * Character Device Driver, for COL331 A3 (hard)
 * Copyright 2023 Aniruddha Deb
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Aniruddha Deb");

#define MINOR_MAX_VERSION 2

static int MAJOR_VERSION = 0;

int writer_open(struct inode *, struct file *);
ssize_t writer_read(struct file *, char __user *, size_t, loff_t *);
ssize_t writer_write(struct file *, const char __user *, size_t, loff_t *);
int writer_release(struct inode *, struct file *);

int reader_open(struct inode *, struct file *);
ssize_t reader_read(struct file *, char __user *, size_t, loff_t *);
ssize_t reader_write(struct file *, const char __user *, size_t, loff_t *);
int reader_release(struct inode *, struct file *);

struct lifo_cdev_data {
	struct cdev reader;
	struct cdev writer;
	// in-memory 1MB buffer
	char* buffer;
	ssize_t head;

	// synchronization
	struct semaphore sem;
	uid_t owner;
	pid_t process;
};

const struct file_operations lifo_cdev_reader_ops = {
	.owner = THIS_MODULE,
	.open = reader_open,
	.read = reader_read,
	.write = reader_write,
	.release = reader_release,
};

const struct file_operations lifo_cdev_writer_ops = {
	.owner = THIS_MODULE,
	.open = writer_open,
	.read = writer_read,
	.write = writer_write,
	.release = writer_release,
};

static struct lifo_cdev_data lifo_cdev;

static struct class *lifo_cdev_class = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////
//
// Only one reader can read from the file at a time, and only one process from the reader can read
// from the device at a time. This is to prevent race conditions that may lead to disorder in the 
// stream
//
// eg. if process 1 has to write "aaaaaa" and process 2 has to write "bbbbbb", and they do it
//     both together, then the stream would have "aaabbababbba". We'd want to prevent this from
//     happening
//
// Therefore, we have the following rules:
// 1. Only one process can access either the lifo_reader or lifo_writer at any given instant
// 2. If another process tries to access the lifo_reader or lifo_writer when they're in use:
//    a. If they're from another user, the request is rejected (we return -EBUSY) till all the 
//       tasks of the current owner are done
//    b. If they're from the same user, the request blocks till the previous accesses are over.
// 3. If the same process tries to both read and write from the device, then since the reads 
//    block till the writes are over (and vice versa), the process may go into deadlock. We 
//    explicitly disallow this and return -EINVAL
//
/////////////////////////////////////////////////////////////////////////////////////////////////

int __acquire_lifo_semaphore(void) {
	if (lifo_cdev.owner != current->loginuid.val && lifo_cdev.owner != 0) return -EBUSY;
	if (lifo_cdev.process == current->pid) return -EINVAL;
	down(&lifo_cdev.sem);
	lifo_cdev.owner = current->loginuid.val;
	lifo_cdev.process = current->pid;
	return 0;
}

void __release_lifo_semaphore(void) {
	lifo_cdev.owner = 0;
	lifo_cdev.process = 0;
	up(&lifo_cdev.sem);
}

int reader_open(struct inode *inode, struct file *file)
{
	int result = __acquire_lifo_semaphore();
	if (result < 0) return result;
	
	printk("lifo_cdev: reader device opened\n");
	return 0;
}

ssize_t reader_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	size_t n_bytes = min(lifo_cdev.head, (ssize_t)size);
	if (lifo_cdev.head == 0) {
		// EOF character doesn't actually exist, and it's dicey to write an EOT character
		// into the byte stream. Just return 0; programs like cat will recognize this 
		// and terminate.
		return 0;
	}
	if (copy_to_user(buf, lifo_cdev.buffer+lifo_cdev.head-n_bytes, n_bytes)) return -22;
	if (n_bytes < (ssize_t)size) {
		// pad with \0 bytes
		clear_user(buf+n_bytes, (ssize_t)size-n_bytes);
	}
	lifo_cdev.head -= n_bytes;
		
	printk("lifo_cdev: reader device read %lu bytes to user and padded %lu bytes with zeros\n", n_bytes, size-n_bytes);
	return size;
}

ssize_t reader_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	printk(KERN_ERR "ERROR: cannot write to reader device\n");
	return -22;
}

int reader_release(struct inode *inode, struct file *file)
{
	__release_lifo_semaphore();
	printk("lifo_cdev: reader device released\n");
	return 0;
}

int writer_open(struct inode *inode, struct file *file)
{
	int result = __acquire_lifo_semaphore();
	if (result < 0) return result;

	printk("lifo_cdev: writer device opened\n");
	return 0;
}

ssize_t writer_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	printk(KERN_ERR "ERROR: cannot read from writer device\n");
	return -22;
}

ssize_t writer_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	int i;
	// reverse byte order!
	if (lifo_cdev.head + size > 1024*1024) {
		// buffer overflow!
		printk(KERN_ERR "ERROR: kernel buffer is full. Unable to write\n");
		return -22;
	}
	if (copy_from_user(lifo_cdev.buffer+lifo_cdev.head, buf, size)) return -22;
	// reverse bytes now
	for (i=0; i<(size+1)/2; i++) {
		char t = lifo_cdev.buffer[lifo_cdev.head + i];
		lifo_cdev.buffer[lifo_cdev.head + i] = lifo_cdev.buffer[lifo_cdev.head + size - 1 - i];
		lifo_cdev.buffer[lifo_cdev.head + size - 1 - i] = t;
	}
	lifo_cdev.head += size;
	printk("lifo_cdev: writer device wrote %lu bytes to device\n", size);
	return size;
}

int writer_release(struct inode *inode, struct file *file)
{
	__release_lifo_semaphore();
	printk("lifo_cdev: writer device released\n");
	return 0;
}

// programmatically setting permissions on the character device
static char* lifo_devnode(struct device *dev, umode_t *mode) {
	if (!mode) return NULL;
	if (dev->devt == MKDEV(MAJOR_VERSION, 0) || dev->devt == MKDEV(MAJOR_VERSION, 1)) {
		*mode = 0666;
	}
	return NULL;
}

static int __init init_drivers(void)
{
	int err;
	dev_t dev;

	// allocate chardev region and assign Major number
	err = alloc_chrdev_region(&dev, 0, MINOR_MAX_VERSION, "lifo_cdev\n");
	if (err != 0) return err;

	MAJOR_VERSION = MAJOR(dev);

	lifo_cdev_class = class_create(THIS_MODULE, "lifo_cdev\n");
	lifo_cdev_class->devnode = lifo_devnode;

	sema_init(&lifo_cdev.sem, 1);
	lifo_cdev.owner = 0;
	lifo_cdev.process = 0;

	// create the reader device first
	cdev_init(&lifo_cdev.reader, &lifo_cdev_reader_ops);
	lifo_cdev.reader.owner = THIS_MODULE;
	cdev_add(&lifo_cdev.reader, MKDEV(MAJOR_VERSION, 0), 1);
	device_create(lifo_cdev_class, NULL, MKDEV(MAJOR_VERSION, 0), NULL, "lifo_reader");

	// Now the writer
	cdev_init(&lifo_cdev.writer, &lifo_cdev_writer_ops);
	lifo_cdev.writer.owner = THIS_MODULE;
	cdev_add(&lifo_cdev.writer, MKDEV(MAJOR_VERSION, 1), 1);
	device_create(lifo_cdev_class, NULL, MKDEV(MAJOR_VERSION, 1), NULL, "lifo_writer");

	// allocate memory for the kernel buffer - 1MB

	lifo_cdev.buffer = (char*)vmalloc(1024*1024);
	lifo_cdev.head = 0;

	return 0;
}

static void __exit destroy_drivers(void)
{
	device_destroy(lifo_cdev_class, MKDEV(MAJOR_VERSION, 0));
	device_destroy(lifo_cdev_class, MKDEV(MAJOR_VERSION, 1));

	class_unregister(lifo_cdev_class);
	class_destroy(lifo_cdev_class);

	unregister_chrdev_region(MKDEV(MAJOR_VERSION,0), MINOR_MAX_VERSION);

	// How do you destroy a semaphore? linux/semaphore.h has no methods for this
	// sem_destroy(&lifo_cdev.sem);
	vfree(lifo_cdev.buffer);
}

module_init(init_drivers);
module_exit(destroy_drivers);
