/*
 * Copyright 2017 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/pci.h>

#include <linux/slab.h>
#include <linux/interrupt.h>


MODULE_AUTHOR("Winefred Washington <winefred@amazon.com>");
MODULE_DESCRIPTION("AWS F1 CL_DRAM_DMA Interrupt Driver Example");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

static int slot = 0x0f;
module_param(slot, int, 0);
MODULE_PARM_DESC(slot, "The Slot Index of the F1 Card");

static struct cdev *kernel_cdev;
static dev_t dev_no;

#define DOMAIN 0
#define BUS 0
#define FUNCTION 0
#define DDR_BAR 3
#define OCL_BAR 0

int f1_open(struct inode *inode, struct file *flip);
int f1_release(struct inode *inode, struct file *flip);
long f1_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param);
ssize_t f1_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t f1_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

struct file_operations f1_fops = {
 .read =           f1_read,
 .write =          f1_write,
 // .unlocked_ioctl = f1_ioctl,
 .open =           f1_open,
 .release =        f1_release
};

struct msix_entry f1_ints[] = {
  {.vector = 0, .entry = 0},
  {.vector = 0, .entry = 1},
  {.vector = 0, .entry = 2},
  {.vector = 0, .entry = 3},
  {.vector = 0, .entry = 4}
};
  
int f1_major = 0;
#define F1_BUFFER_SIZE 4096
unsigned char *f1_buffer;
unsigned char *phys_f1_buffer;

struct pci_dev *f1_dev;
int f1_dev_id;

void __iomem *ocl_base;

#define CFG_REG           0x00
#define CNTL_REG          0x08
#define NUM_INST          0x10
#define MAX_RD_REQ        0x14

#define WR_INSTR_INDEX    0x1c
#define WR_ADDR_LOW       0x20
#define WR_ADDR_HIGH      0x24
#define WR_DATA           0x28
#define WR_LEN            0x2c

#define RD_INSTR_INDEX    0x3c
#define RD_ADDR_LOW       0x40
#define RD_ADDR_HIGH      0x44
#define RD_DATA           0x48
#define RD_LEN            0x4c

#define RD_ERR            0xb0
#define RD_ERR_ADDR_LOW   0xb4
#define RD_ERR_ADDR_HIGH  0xb8
#define RD_ERR_INDEX      0xbc

#define WR_CYCLE_CNT_LOW  0xf0
#define WR_CYCLE_CNT_HIGH 0xf4
#define RD_CYCLE_CNT_LOW  0xf8
#define RD_CYCLE_CNT_HIGH 0xfc

#define WR_START_BIT   0x00000001
#define RD_START_BIT   0x00000002


static void poke_ocl(unsigned int offset, unsigned int data) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  *phy_addr = data;
}

static unsigned int peek_ocl(unsigned int offset) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  return *phy_addr;
}

static unsigned int test_pattern;

static irqreturn_t f1_isr(int a, void *dev_id) {
  printk(KERN_NOTICE "f1_isr\n");
  return IRQ_HANDLED;
}
  
static void run_f1 (void) {
    unsigned int data;
    unsigned int status;

    printk(KERN_INFO "ocl_base: %lx\n", (unsigned long)ocl_base);
    printk(KERN_INFO "peek 0: %x\n", *(unsigned int *)ocl_base);
    printk(KERN_INFO "f1_buffer: %lx\n", (unsigned long)f1_buffer);
    printk(KERN_INFO "phys_f1_buffer: %lx\n", (unsigned long)phys_f1_buffer);
    
    // Enable Incr ID mode, Sync mode, and Read Compare
    poke_ocl(CFG_REG, 0x01000018);

    // Set the max number of read requests
    poke_ocl(MAX_RD_REQ, 0x0000000f);

    poke_ocl(WR_INSTR_INDEX, 0x00000000);                                       // write index
    poke_ocl(WR_ADDR_LOW,    ((unsigned int)(unsigned long)phys_f1_buffer & 0xffffffffl));          // write address low
    poke_ocl(WR_ADDR_HIGH,   (unsigned int)((unsigned long)phys_f1_buffer >> 32l)); // write address high
    poke_ocl(WR_DATA,        test_pattern);                                          // write data
    poke_ocl(WR_LEN,         0x00000001);                                            // write 128 bytes

    printk(KERN_INFO "wr low: %x\n", (unsigned int)peek_ocl(WR_ADDR_LOW));
    printk(KERN_INFO "  high: %x\n", (unsigned int)peek_ocl(WR_ADDR_HIGH));

    poke_ocl(RD_INSTR_INDEX, 0x00000000);                                       // read index
    poke_ocl(RD_ADDR_LOW,    ((unsigned int)(unsigned long)phys_f1_buffer & 0xffffffffl));          // read address low
    poke_ocl(RD_ADDR_HIGH,   (unsigned int)(((unsigned long)phys_f1_buffer) >> 32l));  // read address high
    poke_ocl(RD_DATA,        test_pattern);                                             // read data
    poke_ocl(RD_LEN,         0x00000001);                                               // read 128 bytes

    printk(KERN_INFO "rd low: %x\n", (unsigned int)peek_ocl(RD_ADDR_LOW));
    printk(KERN_INFO "  high: %x\n", (unsigned int)peek_ocl(RD_ADDR_HIGH));

    // Number of instructions, zero based ([31:16] for read, [15:0] for write)
    poke_ocl(NUM_INST, 0x00000000);

    // Start writes and reads
    poke_ocl(CNTL_REG, WR_START_BIT | RD_START_BIT);

    // for fun, read status
    status = peek_ocl(CNTL_REG);
    printk(KERN_INFO "status: %d\n", status);

    // Stop F1
    poke_ocl(CNTL_REG, 0x00000000);

    // for fun, read count registers
    data = peek_ocl(WR_CYCLE_CNT_LOW);
    printk(KERN_INFO "Write Cycle Count Low: %d\n", data);

    data = peek_ocl(RD_CYCLE_CNT_LOW);
    printk(KERN_INFO "Read Cycle Count Low: %d\n", data);

}

static int __init f1_init(void) {
  int result;

  printk(KERN_NOTICE "Installing f1 module\n");

  f1_dev = pci_get_domain_bus_and_slot(DOMAIN, BUS, PCI_DEVFN(slot,FUNCTION));
  if (f1_dev == NULL) {
    printk(KERN_ALERT "f1_driver: Unable to locate PCI card.\n");
    return -1;
  }

  printk(KERN_INFO "vendor: %x, device: %x\n", f1_dev->vendor, f1_dev->device);

  result = pci_enable_device(f1_dev);
  printk(KERN_INFO "Enable result: %x\n", result);

  result = pci_request_region(f1_dev, DDR_BAR, "DDR Region");
  if (result <0) {
    printk(KERN_ALERT "f1_driver: cannot obtain the DDR region.\n");
    return result;
  }


  result = pci_request_region(f1_dev, OCL_BAR, "OCL Region");
  if (result <0) {
    printk(KERN_ALERT "f1_driver: cannot obtain the OCL region.\n");
    return result;
  }

  ocl_base = (void __iomem *)pci_iomap(f1_dev, OCL_BAR, 0);   // BAR=0 (OCL), maxlen = 0 (map entire bar)


  result = alloc_chrdev_region(&dev_no, 0, 1, "f1_driver");   // get an assigned major device number

  if (result <0) {
    printk(KERN_ALERT "f1_driver: cannot obtain major number.\n");
    return result;
  }

  f1_major = MAJOR(dev_no);
  printk(KERN_INFO "The f1_driver major number is: %d\n", f1_major);

  kernel_cdev = cdev_alloc();
  kernel_cdev->ops = &f1_fops;
  kernel_cdev->owner = THIS_MODULE;

  result = cdev_add(kernel_cdev, dev_no, 1);

  if (result <0) {
    printk(KERN_ALERT "f1_driver: Unable to add cdev.\n");
    return result;
  }

  f1_buffer = kmalloc(F1_BUFFER_SIZE, GFP_DMA | GFP_USER);    // DMA buffer, do not swap memory
  phys_f1_buffer = (unsigned char *)virt_to_phys(f1_buffer);  // get the physical address for later

  test_pattern = 0x44434241;  // initialize test_pattern

  // allocate MSIX resources
  result = pci_enable_msix(f1_dev, f1_ints, 5);
  printk(KERN_NOTICE "pci_enable_msix result: %x, %x\n", result, f1_ints[0].vector);
  
  request_irq(f1_ints[0].vector, f1_isr, 0, "f1_driver", &f1_dev_id);
  request_irq(f1_ints[1].vector, f1_isr, 0, "f1_driver", &f1_dev_id);
  request_irq(f1_ints[2].vector, f1_isr, 0, "f1_driver", &f1_dev_id);
  request_irq(f1_ints[3].vector, f1_isr, 0, "f1_driver", &f1_dev_id);
  request_irq(f1_ints[4].vector, f1_isr, 0, "f1_driver", &f1_dev_id);
  
  return 0;

}

static void __exit f1_exit(void) {

  cdev_del(kernel_cdev);

  free_irq(f1_ints[0].vector, &f1_dev_id);
  
  // free up MSIX resources
  pci_disable_msix(f1_dev);
  
  unregister_chrdev_region(dev_no, 1);

  if (f1_buffer != NULL)
    kfree(f1_buffer);
  

  if (f1_dev != NULL) {
    pci_iounmap(f1_dev, ocl_base);
    pci_disable_device(f1_dev);

    pci_release_region(f1_dev, DDR_BAR);    // release DDR & OCL regions
    pci_release_region(f1_dev, OCL_BAR);

    pci_dev_put(f1_dev);                    // free device memory
  }

  printk(KERN_NOTICE "Removing f1 module\n");
}

module_init(f1_init);

module_exit(f1_exit);

int f1_open(struct inode *inode, struct file *filp) {
  printk(KERN_NOTICE "f1_driver opened\n");
  return 0;
}

int f1_release(struct inode *inode, struct file *filp) {
  printk(KERN_NOTICE "f1_driver closed\n");
  return 0;
}

ssize_t f1_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
  unsigned long result;
  size_t n;

  printk(KERN_INFO "user read size: %zd\n", count);

  n = (count > F1_BUFFER_SIZE) ? F1_BUFFER_SIZE : count;
  result = copy_to_user(buf, f1_buffer, count);

  if (result != 0)
    printk(KERN_INFO "Could not copy %ld bytes\n", result);


  *f_pos += n - result;

  return (n-result);
}

ssize_t f1_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
  unsigned long result;
  size_t n;

  printk(KERN_INFO "user write buffer: %s\n", buf);
  printk(KERN_INFO "user write size: %zd\n", count);

  n = (count > F1_BUFFER_SIZE) ? F1_BUFFER_SIZE : count;

  // put data in driver buffer
  result = copy_from_user(f1_buffer, buf, n);

  if (*buf != '0')
    run_f1();

  if (result != 0)
    printk(KERN_INFO "Could not copy %ld bytes\n", result);

  *f_pos += n - result;

  return (n - result);
}

