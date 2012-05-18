/**
 * File: asgn2.c
 * Date: 09/04/2011
 * Author: Edward Hills 
 * Version: 1.0
 *
 * This is a module which serves as an interrupt handler and will read a byte at a time from the parallel port.
 *
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/poll.h>
#include <linux/ioport.h>
#include <linux/sched.h>

#define MYDEV_NAME "asgn2"
#define MYIOC_TYPE 'k'
#define CBUF_SIZE 64

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Edward Hills");
MODULE_DESCRIPTION("COSC440 asgn2");

/**
 * The node structure for the memory page linked list.
 */ 
typedef struct page_node_rec {
    struct list_head list;
    struct page *page;
} page_node;

/**
 *  Circular Buffer struct.
 */
typedef struct circular_buffer_t {
    int start;
    int count;
    char buffer[CBUF_SIZE];
} circular_buffer;

typedef struct asgn2_dev_t {
    dev_t dev;            /* the device */
    struct cdev *cdev;
    struct list_head mem_list; 
    int num_pages;        /* number of memory pages this module currently holds */
    size_t data_size;     /* total data size in this module */
    atomic_t nprocs;      /* number of processes accessing this device */ 
    atomic_t max_nprocs;  /* max number of processes accessing this device */
    atomic_t nevents;       /* number of events */
    struct kmem_cache *cache;      /* cache memory */
    struct class *class;     /* the udev class */
    struct device *device;   /* the udev device node */
} asgn2_dev;

asgn2_dev asgn2_device;
circular_buffer cbuf;                     /* make circular buffer */

int *session_ends;                        /* Keep track of session (file) */
int session_count = 0;
int session_read = 0;

int finished_reading = 0;

int asgn2_major = 0;                      /* major number of module */  
int asgn2_minor = 0;                      /* minor number of module */
int asgn2_dev_count = 1;                  /* number of devices */

unsigned int par_irq = 7;
unsigned long parport_base = 0x378;

module_param(asgn2_major, int, S_IRUGO);
MODULE_PARM_DESC(asgn2_major, "device major number");

/*
 * Check whether circular buffer is full.
 */
int is_cb_full(void) {
    return cbuf.count == CBUF_SIZE;
}

/*
 * Check whether circular buffer is empty.
 */
int is_cb_empty(void) {
    return cbuf.count == 0;
}

/**
 * Add byte into circular buffer.
 */
void cbuffer_add(char byte) {
    int end = (cbuf.start + cbuf.count) % CBUF_SIZE;

    cbuf.buffer[end] = byte;
    if (is_cb_full()) {
        cbuf.start = (cbuf.start + 1) % CBUF_SIZE;
    } else {
        cbuf.count++;
    }
}

/**
 * Remove byte from circular buffer.
 */
char cbuffer_get_byte(void) {
    char byte;

    byte = cbuf.buffer[cbuf.start];
    cbuf.start = (cbuf.start + 1) % CBUF_SIZE;
    cbuf.count--;
    return byte;

}

/**
 * This function frees all memory pages held by the module.
 */
void free_memory_pages(void) {
    page_node *curr;
    page_node *temp;

    // free all the pages then delete page_nodes 
    list_for_each_entry_safe(curr, temp, &(asgn2_device.mem_list), list) {
        if (curr->page != NULL) {
            __free_page(curr->page);
        }
        list_del(&(curr->list));
        kmem_cache_free(asgn2_device.cache, curr);
    }

    asgn2_device.num_pages = 0;
    asgn2_device.data_size = 0;
}

/* write byte to the page queue */
int write_to_page_list(char byte) {
    int begin_offset = asgn2_device.data_size % PAGE_SIZE;

    struct list_head *ptr = asgn2_device.mem_list.prev;
    page_node *curr;

    curr = list_entry(ptr, page_node, list);
    if (begin_offset == 0) {

        if ((curr = kmem_cache_alloc(asgn2_device.cache, GFP_KERNEL)) == NULL) {
            printk(KERN_ERR "Not enough memory left\n");
            return -ENOMEM;
        }

        if ((curr->page = alloc_page(GFP_KERNEL)) == NULL) {
            printk(KERN_ERR "Not enough memory left\n");
            return -ENOMEM;
        }
        INIT_LIST_HEAD(&(curr->list));
        list_add_tail(&(curr->list), &(asgn2_device.mem_list));
        asgn2_device.num_pages++;
        ptr = asgn2_device.mem_list.prev;
    }

    // write to the page
    memcpy(page_address(curr->page) + begin_offset, &byte, 1);
    asgn2_device.data_size += sizeof(char);

    return 1;
} 

/* wait queue for read processes */
static DECLARE_WAIT_QUEUE_HEAD(read_wq);

/* Bottom half */
void write(unsigned long t_arg) {
    char byte;

    /* write all bytes from cbuffer into page list */
    while (!is_cb_empty()) {
        byte = cbuffer_get_byte();

        write_to_page_list(byte);

        atomic_inc(&asgn2_device.nevents);
        if (byte == '\0') {
            if (session_count != 0) {
                session_ends = krealloc(session_ends, sizeof(int) * (session_count + 1), GFP_KERNEL);
            } else {
                session_ends = kmalloc(sizeof(int), GFP_KERNEL);
            }
            session_ends[session_count] = asgn2_device.data_size - 1;
            session_count++;
            
            // only wake up reader if theres a whole file to read
            wake_up_interruptible(&read_wq);
        }
    }

}

/* declare tasklet */
static DECLARE_TASKLET(tasklet, write, (unsigned long)&cbuf);

/* irq handler i.e top half */
irqreturn_t irq_handler(int irq, void *dev_id) {
    char byte;

    byte = inb_p(parport_base);
    byte &= 127;
    cbuffer_add(byte);

    tasklet_schedule(&tasklet);

    return IRQ_HANDLED;

}

/* wait queue for read processes */
static DECLARE_WAIT_QUEUE_HEAD(open_wq);

/**
 * This function opens the virtual disk, if it is opened in the write-only
 * mode, all memory pages will be freed.
 */
int asgn2_open(struct inode *inode, struct file *filp) {

    if ((filp->f_flags & O_ACCMODE) == O_WRONLY || (filp->f_flags & O_ACCMODE) == O_RDWR) {
        printk(KERN_INFO "Cannot access device for writing\n");
               return -EACCES;
    } 

    // check there arent too many proccesses already
    wait_event_interruptible(open_wq, 
            (atomic_read(&asgn2_device.nprocs) < atomic_read(&asgn2_device.max_nprocs)));
    finished_reading = 0;
    if (signal_pending(current)) {
        printk(KERN_INFO "process %i woken up by a signal\n",
                current->pid);
        return -ERESTARTSYS;
    }
    printk(KERN_INFO "Current running PID: %d\n", current->pid);
    atomic_inc(&asgn2_device.nprocs);

    printk(KERN_INFO "Opening device: %s\n", MYDEV_NAME);
    printk(KERN_INFO "MAJOR number = %d, MINOR number = %d\n",
            imajor(inode), iminor(inode));
    return 0;
}

/*
 * This function releases the virtual disk, but nothing needs to be done
 * in this case. 
 */
int asgn2_release (struct inode *inode, struct file *filp) {

    atomic_dec(&asgn2_device.nprocs);
    printk(KERN_INFO " closing character device: %s\n\n", MYDEV_NAME);
    wake_up_interruptible(&open_wq);
    return 0;
}

/* Flag to see if it file has bee nread */

/**
 * This function reads contents of the virtual disk and writes to the user 
 */
ssize_t asgn2_read(struct file *filp, char __user *buf, size_t count,
        loff_t *f_pos) {
    size_t size_read = 0;     /* size read from virtual disk in this function */
    size_t begin_offset;      /* the offset from the beginning of a page to
                                 start reading */
    int begin_page_no = *f_pos / PAGE_SIZE; /* the first page which contains
                                               the requested data */
    int curr_page_no = 0;     /* the current page number */
    size_t curr_size_read;    /* size read from the virtual disk in this round */
    size_t size_to_be_read;   /* size to be read in the current round in 
                                 while loop */

    int i;
    struct list_head *ptr = &asgn2_device.mem_list;
    page_node *curr;
    page_node *temp;

    /* check to see if it's file has been read */
    if (finished_reading == 1) {
        return 0;
    }

    /* check if a whole file has been written, if not go in queue and wait */
    wait_event_interruptible(read_wq, (session_count > session_read));
    if (signal_pending(current)) {
        printk(KERN_INFO "process %i woken up by a signal\n",
                current->pid);
        return -ERESTARTSYS;
    }

    if (*f_pos >= asgn2_device.data_size) {
        printk(KERN_ERR "Reached end of the device on a read");
        return 0;
    }

    /* set fpos = the previous files end */
    if (session_read != 0) {
        *f_pos = session_ends[session_read -1] + 1;
    }

    /* read from pages */
    begin_offset = *f_pos % PAGE_SIZE;

    /* Make count only the distance from where i am now to my nul */
    count = (session_ends[session_read] - *f_pos); // tell count to only read up to the null
    session_read++;

    list_for_each_entry_safe(curr, temp, ptr, list) {

        if (begin_page_no <= curr_page_no) {
            do {
                size_to_be_read = min(((int)PAGE_SIZE - begin_offset), (count - size_read));
                curr_size_read = size_to_be_read - copy_to_user(buf + size_read,
                        page_address(curr->page) + begin_offset, size_to_be_read);

                /* check if ive read a page and if so free it */
                if (curr_size_read + begin_offset == PAGE_SIZE) {
                        __free_page(curr->page);
                        list_del(&(curr->list));
                        kmem_cache_free(asgn2_device.cache, curr);

                        asgn2_device.num_pages -= 1;
                        asgn2_device.data_size -= PAGE_SIZE;
                        /* Decrement the end position for this file by page_size */
                        for (i = session_read -1; i < session_count; i++) {
                            session_ends[i] -= PAGE_SIZE;
                        }

                }
                size_read += curr_size_read;
                size_to_be_read -= curr_size_read;
                begin_offset += curr_size_read;
            } while(size_to_be_read > 0);
            begin_offset = 0;
            if (size_read == count) {
                break;
            }
        }
        curr_page_no++;
    }
    printk(KERN_INFO "Read %d bytes\n", (int)size_read);
    // minus the events i just read (plus nul)
    atomic_sub(size_read+1, &asgn2_device.nevents);
    *f_pos += size_read + 1;
    /* eof flag */
    finished_reading = 1;
    return size_read + 1;
}

/**
 * This function writes from the user buffer to the virtual disk of this
 * module
 */

#define SET_NPROC_OP 1
#define TEM_SET_NPROC _IOW(MYIOC_TYPE, SET_NPROC_OP, int) 

/**
 * The ioctl function, which nothing needs to be done in this case.
 */
long asgn2_ioctl (struct file *filp, unsigned int cmd, unsigned long arg) {
    int nr;
    int new_nprocs;
    int result;

    // check that the command is actually for my type of device
    if (_IOC_TYPE(cmd) != MYIOC_TYPE) {
        printk(KERN_WARNING "Invalid comand CMD=%d, for this type.\n", cmd);
        return -EINVAL;
    }

    nr = _IOC_NR(cmd);
    if (nr == SET_NPROC_OP) {
        if(get_user(new_nprocs, (int *)arg) != 0) {
            printk(KERN_ERR "Cannot read arg value.\n");
            return -1;
        }

        // make sure im not lowering maxprocs when more processes are accessing this device
        if (new_nprocs < atomic_read(&asgn2_device.nprocs) ) {
            printk(KERN_ERR "Cannot set maximum number of processes to %d because too many processes are currently accessing this device.\n", new_nprocs);
            result = -1;
        } else {
            atomic_set(&asgn2_device.max_nprocs, new_nprocs);
            result = 0;
        }
        return result;
    }

    printk(KERN_WARNING "Invalid comand nr=%d, for this type.\n", nr);
    return -ENOTTY;
}


/**
 * Displays information about current status of the module,
 * which helps debugging.
 */
int asgn2_read_procmem(char *buf, char **start, off_t offset, int count,
        int *eof, void *data) {
    int result;

    // write data about this device to proc
    result = snprintf(buf + offset, count + 1, "Character device driver: %s\n", MYDEV_NAME);  
    result += snprintf(buf + offset + result, count + 1, "Number of pages used: %d\n", (int)asgn2_device.num_pages);  
    result += snprintf(buf + offset + result, count + 1, "Size of this device: %d\n", (int)asgn2_device.data_size);  
    result += snprintf(buf + offset + result, count + 1, "Number of processess accessing this device: %d\n", (int)atomic_read(&asgn2_device.nprocs));  

    // set eof so we know we are done writing
    if (result <= offset + count) {
        *eof = 1;
    }

    *start = buf + offset;
    result -= offset;

    if (result > count)
        result = count;
    if (result < 0)
        result = 0;

    return result;
}


struct file_operations asgn2_fops = {
    .owner = THIS_MODULE,
    .read = asgn2_read,
    .unlocked_ioctl = asgn2_ioctl,
    .open = asgn2_open,
    .release = asgn2_release
};


/**
 * Initialise the module and create the master device
 */
int __init asgn2_init_module(void){
    int result;

    asgn2_device.dev = MKDEV(asgn2_major, 0);
    atomic_set(&asgn2_device.max_nprocs, 1);
    atomic_set(&asgn2_device.nprocs, 0);
    atomic_set(&asgn2_device.nevents, 0);
    asgn2_device.data_size = 0;
    asgn2_device.num_pages = 0;

    cbuf.start = 0;
    cbuf.count = 0;

    if (asgn2_major) {
        // try register given major number
        result = register_chrdev_region(asgn2_device.dev, asgn2_dev_count, "Eds_char_device");

        if (result < 0) {
            // uh oh didnt work better get system to allocate it
            printk(KERN_WARNING "Can't use the major number %d; trying automatic allocation..\n", asgn2_major);

            // check if system can give me a number or die
            if ((result = alloc_chrdev_region(&asgn2_device.dev, asgn2_major, asgn2_dev_count, MYDEV_NAME)) < 0) {
                printk(KERN_ERR "Failed to allocate character device region\n");
                return -1;
            }
            asgn2_major = MAJOR(asgn2_device.dev);
        }
    } 
    else {
        // user hasnt given me a major number so ill make my own
        if ((result = alloc_chrdev_region(&asgn2_device.dev, asgn2_major, asgn2_dev_count, MYDEV_NAME)) < 0) {
            printk(KERN_ERR "Failed to allocate character device region\n");
            return -1;
        }
        asgn2_major = MAJOR(asgn2_device.dev);
    }

    // allocate cdev
    if (!(asgn2_device.cdev = cdev_alloc())) {
        printk(KERN_ERR "cdev_alloc() failed.\n");
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
        return -1;
    }

    // init cdev
    cdev_init(asgn2_device.cdev, &asgn2_fops);
    asgn2_device.cdev->owner = THIS_MODULE;

    // add cdev
    if (cdev_add(asgn2_device.cdev, asgn2_device.dev, asgn2_dev_count) < 0) {
        printk(KERN_ERR "cdev_add() failed.\n");
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
        return -1;
    }

    // initiliase page list
    INIT_LIST_HEAD(&(asgn2_device.mem_list));

    // setup kmem cache
    if ((asgn2_device.cache = kmem_cache_create("asgn2_cache", sizeof(page_node), 0, 0, NULL)) == NULL) {
        printk(KERN_ERR "Cannot allocate kmem cache.\n");
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
        return -ENOMEM;
    }


    // initialise proc 
    if (create_proc_read_entry(MYDEV_NAME, S_IRUSR | S_IRGRP | S_IROTH, NULL, asgn2_read_procmem, NULL) == NULL) {
        printk(KERN_ERR "Error: Could not initialize /proc/%s/\n", MYDEV_NAME);
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
        kmem_cache_destroy(asgn2_device.cache);
        return -ENOMEM;
    }

    /* request parallel port region */
    if(request_region(parport_base, 3, MYDEV_NAME) == NULL) {
        printk(KERN_ERR "Cannot request region for parallel port at this time.\n");
        cdev_del(asgn2_device.cdev);
        unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);
        kmem_cache_destroy(asgn2_device.cache);
        remove_proc_entry(MYDEV_NAME, NULL);
        return -1;
    }

    /* request irq */
    if (request_irq(par_irq, irq_handler, 0, MYDEV_NAME, &asgn2_device) != 0) {
        printk(KERN_ERR "Cannot setup irq request.\n");
        result = -1;
        goto fail_class;
    }
    outb_p(inb_p(parport_base + 2) | 0x10, parport_base + 2);


    // create class
    asgn2_device.class = class_create(THIS_MODULE, MYDEV_NAME);
    if (IS_ERR(asgn2_device.class)) {
        printk(KERN_WARNING "%s: can't create udev class\n", MYDEV_NAME);
        result = -ENOMEM;
        goto fail_class;
    }

    // create device
    asgn2_device.device = device_create(asgn2_device.class, NULL, 
            asgn2_device.dev, "%s", MYDEV_NAME);
    if (IS_ERR(asgn2_device.device)) {
        printk(KERN_WARNING "%s: can't create udev device\n", MYDEV_NAME);
        result = -ENOMEM;
        goto fail_device;
    }

    printk(KERN_WARNING "set up udev entry\n");
    printk(KERN_WARNING "Hello world from %s\n\n", MYDEV_NAME);
    return 0;

    // cleanup if class init fails
fail_class:
    free_irq(par_irq, &asgn2_device);
    release_region(parport_base, 3);
    remove_proc_entry(MYDEV_NAME, NULL);
    list_del_init(&asgn2_device.mem_list);
    kmem_cache_destroy(asgn2_device.cache);
    cdev_del(asgn2_device.cdev);
    unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);

    return result;

    // cleanup if device init fails
fail_device:
    class_destroy(asgn2_device.class);
    goto fail_class;
}

/**
 * Finalise the module
 */
void __exit asgn2_exit_module(void){
    // free and destroy things set up in reverse order
    device_destroy(asgn2_device.class, asgn2_device.dev);
    class_destroy(asgn2_device.class);
    synchronize_irq(par_irq);
    free_irq(par_irq, &asgn2_device);
    release_region(parport_base, 3);
    remove_proc_entry(MYDEV_NAME, NULL);
    printk(KERN_WARNING "cleaned up udev entry\n");

    free_memory_pages();
    kmem_cache_destroy(asgn2_device.cache);
    cdev_del(asgn2_device.cdev);
    unregister_chrdev_region(asgn2_device.dev, asgn2_dev_count);

    printk(KERN_WARNING "Good bye from %s\n", MYDEV_NAME);
}


module_init(asgn2_init_module);
module_exit(asgn2_exit_module);

