
## F1 FPGA Application Note
# How to Use PCIe Interrupts
## Version 1.0

## Introduction

When using interrupts a developer can use the device drivers supplied with the F1 Developer's Kit or write their own. This application note describes the basic kernel calls needed for a developer to write a custom interrupt service routine (ISR) and provides an example that demonstrates those calls.

## Concepts

To interrupt the host CPU, the F1 Shell (SH) uses a method called Message Signaled Interrupts, MSI. MSI interrupts are not sent using dedicated wires between the device and the CPU's interrupt controller. MSI interrupts use the PCIe bus to transmit a message from the device to grab the attention of the CPU.

MSI first appeared in PCI 2.2 and enabled a device to generate up to 32 interrupts. In PCI 3.0, an extended version of MSI was created called MSI-X, and MSI-X increases the number of possible interrupts from 32 to 2048. A F1 custom logic (CL) accelerator uses this latest MSI-X protocol and can generate up to 16 user-defined interrupts.

Each of these interrupts may call a different ISR, or one or more of the interrupts can call the same ISR with a parameter to differentiate them. The ISR is executed in kernel space, not user space; therefore, care must be taken when writing a custom ISR to prevent noticeable delays in other ISRs or user space applications. In additional goal of this application note is to illustrate how to place the majority of interrupt processing in a user space process instead of inside the kernel module.

When the device wants to send an interrupt the SH PCIe block is notified by asserting one of 16 user-interrupt signals. The PCIe will acknowledge the interrupt by asserting the acknowledge signal. The PCIe block will issue a MSI-X message to the PCIe bridge located in the server, and the bridge notifies the CPU.

Before using interrupts, they must be [enabled in PCIe configuration space](#enabling-interrupts-in-pcie-configuration-space), [registered with the kernel](#registering-interrupts-with-the-kernel), and [configured in the PCIe block](#configuring-interrupts-in-the-pcie-dma-subsystem).

### Enabling Interrupts in PCIe Configuration Space

```
#define NUM_OF_USER_INTS 16

struct msix_entry f1_ints[] = {
  {.vector = 0, .entry = 0},
  {.vector = 0, .entry = 1},
...
  {.vector = 0, .entry = 15}
};

  // allocate MSIX resources
  result = pci_enable_msix(f1_dev, f1_ints, NUM_OF_USER_INTS);

```

### Registering Interrupts with the Kernel


```
  for(i=0; i<NUM_OF_USER_INTS; i++) {
    f1_dev_id[i] = kmalloc(sizeof(int), GFP_DMA | GFP_USER);
    *f1_dev_id[i] = i;
    request_irq(f1_ints[i].vector, f1_isr, 0, "f1_driver", f1_dev_id[i]);
  }
  
```

### Configuring Interrupts in the PCIe DMA Subsystem

```
    // Enable Interrupt Mask (This step seems a little backwards.)    
    rc = fpga_pci_poke(dma_bar_handle, dma_reg_addr(IRQ_TGT, 0, 0x004), 0xffff);
    printf("IRQ Block User Interrupt Enable Mask read_data: %0x\n", read_data);
    
    // point each user interrupt to a different vector
    rc = fpga_pci_poke(dma_bar_handle, dma_reg_addr(IRQ_TGT, 0, 0x080), 0x03020100);    
    rc |= fpga_pci_poke(dma_bar_handle, dma_reg_addr(IRQ_TGT, 0, 0x084), 0x07060504);    
    rc |= fpga_pci_poke(dma_bar_handle, dma_reg_addr(IRQ_TGT, 0, 0x088), 0x0b0a0908);
    rc |= fpga_pci_poke(dma_bar_handle, dma_reg_addr(IRQ_TGT, 0, 0x08c), 0x0f0e0d0c);
    
```

#### Accessing CL Registers from Software

The intended purpose of the OCL port is to connect a CL's control/status registers to the PCIe bus. When the F1 card is enumerated the registers are placeed into BAR 0. In order to access these registers, they must be mapped into the device driver's address space. To do this requires four function calls.

```
  // Retrieve the device specific information about the card
  f1_dev = pci_get_domain_bus_and_slot(DOMAIN, BUS, PCI_DEVFN(slot,FUNCTION));

  ...
  // Initialize the card
  result = pci_enable_device(f1_dev);

  ...
  // Mark the region as owned
  result = pci_request_region(f1_dev, OCL_BAR, "OCL Region");

  ...
  // Map the entire BAR 0 region into the driver's address space
  ocl_base = (void __iomem *)pci_iomap(f1_dev, OCL_BAR, 0);   // BAR=0 (OCL), maxlen = 0 (map entire bar)

```
All OCL addresses are relative to the starting address of the BAR.




### Compiling and Running the ATG Device Driver
To run this example, launch an F1 instance, clone the aws-fpga Github repository, and download the latest [app note files](./f3fbb176cfa44bf73b4c201260f52f25).

Use the ```fpga-load-local-image``` command to load the FPGA with the CL_DRAM_DMA AFI. *(If you are running on a 16xL, load the AFI into slot 0.)*

Based on your instance size, type one of the following commands:
```
$ sudo lspci -vv -s 0000:00:0f.0  # 16xL
```
Or
```
$ sudo lspci -vv -s 0000:00:1d.0  # 2xL
```
The command will produce output similar to the following:
```
00:0f.0 Memory controller: Device 1d0f:f001
        Subsystem: Device fedc:1d51
        Physical Slot: 15
        Control: I/O- Mem+ BusMaster+ SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-
        Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
        Latency: 0
        Region 0: Memory at c4000000 (32-bit, non-prefetchable) [size=32M]
        Region 1: Memory at c6000000 (32-bit, non-prefetchable) [size=2M]
        Region 2: Memory at 5e000410000 (64-bit, prefetchable) [size=64K]
        Region 4: Memory at 5c000000000 (64-bit, prefetchable) [size=128G]
```

Check to make sure the output displays ```BusMaster+```. This indicates that the device is allowed to perform bus mastering operations. It is not unusual to have the Bus Master Enable turned off, ```BusMaster-```, by the OS when loading or unload a device driver or after an error. If the Bus Master Enable is disabled, it can be enabled again by typing:
```
$ sudo setpci -v -s 0000:00:0f.0 COMMAND=06
```
The OCL interface is mapped to Region 0. Accesses to this region will produce AXI transactions at the OCL port of the CL. The ATG registers are located in this region.

Next, compile the ATG device driver and test program.
```
$ make            # compiles the device driver
$ make test       # compiles the test program
```
Now we are ready to install the device driver. Type the following command:
```
$ sudo insmod f1_driver.ko slot=0x0f           # 16xl
```
Or
```
$ sudo insmod f1_driver.ko slot=0x1d           # 2xl
```
You should not see any errors and it should silently return to the command prompt. To check to see if the driver loaded, type:
```
$ dmesg
```
This command will print the message buffer from the kernel. Since the device driver is a kernel module, special prints are used to place messages in this buffer. You should see something similar to the following:
```

To understand how to access CL registers mapped on the OCL interface, take a look at the poke_ocl and peek_ocl functions in the [f1_driver.c](./f3fbb176cfa44bf73b4c201260f52f25#file-f1_driver-c) file.
```
static void poke_ocl(unsigned int offset, unsigned int data) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  *phy_addr = data;
}

```
static unsigned int peek_ocl(unsigned int offset) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  return *phy_addr;
}
```
The ocl_base variable holds the starting address of the OCL BAR and is found by using ```pci_iomap()```.

## For Further Reading

### [AWS F1 FPGA Developer's Kit](https://github.com/aws/aws-fpga)
### [AWS F1 Shell Interface Specification](https://github.com/aws/aws-fpga/blob/master/hdk/docs/AWS_Shell_Interface_Specification.md)
### [The MSI Driver Guide HOWTO](https://www.kernel.org/doc/Documentation/PCI/MSI-HOWTO.txt)
### [Information on Using Spinlocks to Provide Exclusive Access in Kernel.](https://www.kernel.org/doc/Documentation/locking/spinlocks.txt)
### [How To Write Linux PCI Drivers](https://www.kernel.org/doc/Documentation/PCI/pci.txt)

## Revision History

|      Date      | Version |           Revision          |   Shell    |   Developer   |
| -------------- |  :---:  | --------------------------- |   :---:    |     :---:     |
|  Nov. 22, 2017 |   1.0   | Initial Release             | 0x071417d3 | W. Washington |

