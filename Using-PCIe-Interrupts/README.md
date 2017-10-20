
## F1 FPGA Application Note
# How to Use PCIe Interrupts
## Version 1.0

## Introduction


## Concepts


### Accessing CL Registers from Software

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
[ 6727.147510] Installing atg module
[ 6727.153025] vendor: 1d0f, device: f001
[ 6727.156472] Enable result: 0
[ 6727.165227] The f1_driver major number is: 247
```
The f1_driver will load and an unused major number will be assigned by the OS. Please use the major number (247 is this example) when creating the device special file:
```
$ sudo mknod /dev/f1_driver c 247 0
```
You will not need to create this device file again unless you reboot your instance.
You can now run the test:
```
$ sudo ./f1_test
msg_result: This is a test
msg_result: DCBAECBAFCBAGCB      # expected result
```
The two prints are output by the test program snippet shown in Figure 1. The test program copies a string to the device driver buffer using the ```pwrite()```. The first line is simply a read of the buffer and a print of its contents using ```pread()```. The CL was not accessed.
The second ```pwrite()``` uses a non-zero offset. This is detected by device driver and is used to run ATG logic. The logic overwrites the buffer with a test pattern. This time when the buffer is read, the test pattern is returned. 
```
  // write msg == read msg
  pwrite(fd, test_msg, sizeof(test_msg), 0);
  pread(fd, msg_result, sizeof(test_msg), 0);
  printf("msg_result: %s\n", msg_result);

  // write msg != read msg
  pwrite(fd, test_msg, sizeof(test_msg), 0x100);
  pread(fd, msg_result, sizeof(test_msg), 0);
  printf("msg_result: %s\n", msg_result);
```
*Figure 1. ATG Test Program Body*

With normal file I/O, the ```pwrite/pread``` offset argument is used to move the file pointer to various locations within the file. In this example the offset argument is used by the device driver to enable a different behavior. For your application, you may use the offset to program different addresses within the CL.

During development of your device driver and CL, it is a good idea to periodically check the FPGA metrics to look for errors. Simply, type:
```
$ sudo fpga-describe-local-image -S 0 –M
```
Figure 2 shows an example where the PCIM generated a Bus Master Enable error caused when the CL accessed an invalid address. The ```pcim-axi-protocol-bus-master-enable-error``` field is set along with the error address and count.

To clear the counters, type:
```
$ sudo fpga-describe-local-image -S 0 –C
```

```
AFI          0       agfi-02948a33d1a0e9665  loaded            0        ok               0       0x071417d3
AFIDEVICE    0       0x1d0f      0xf001      0000:00:0f.0
sdacl-slave-timeout=0
virtual-jtag-slave-timeout=0
ocl-slave-timeout=0
bar1-slave-timeout=0
dma-pcis-timeout=0
pcim-range-error=0
pcim-axi-protocol-error=1
pcim-axi-protocol-4K-cross-error=0
pcim-axi-protocol-bus-master-enable-error=1
pcim-axi-protocol-request-size-error=0
pcim-axi-protocol-write-incomplete-error=0
pcim-axi-protocol-first-byte-enable-error=0
pcim-axi-protocol-last-byte-enable-error=0
pcim-axi-protocol-bready-error=0
pcim-axi-protocol-rready-error=0
pcim-axi-protocol-wchannel-error=0
sdacl-slave-timeout-addr=0x0
sdacl-slave-timeout-count=0
virtual-jtag-slave-timeout-addr=0x0
virtual-jtag-slave-timeout-count=0
ocl-slave-timeout-addr=0x0
ocl-slave-timeout-count=0
bar1-slave-timeout-addr=0x0
bar1-slave-timeout-count=0
dma-pcis-timeout-addr=0x0
dma-pcis-timeout-count=0
pcim-range-error-addr=0x0
pcim-range-error-count=0
pcim-axi-protocol-error-addr=0x85000
pcim-axi-protocol-error-count=4
pcim-write-count=2
pcim-read-count=0
DDR0
   write-count=0
   read-count=0
DDR1
   write-count=0
   read-count=0
DDR2
   write-count=0
   read-count=0
DDR3
   write-count=0
   read-count=0
```
*Figure 2. fpga-describe-local-image Metrics Dump*

To understand how to access CL registers mapped on the OCL interface, take a look at the poke_ocl and peek_ocl functions in the [f1_driver.c](./f3fbb176cfa44bf73b4c201260f52f25#file-f1_driver-c) file.
```
static void poke_ocl(unsigned int offset, unsigned int data) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  *phy_addr = data;
}

static unsigned int peek_ocl(unsigned int offset) {
  unsigned int *phy_addr = (unsigned int *)(ocl_base + offset);
  return *phy_addr;
}
```
The ocl_base variable holds the starting address of the OCL BAR and is found by using ```pci_iomap()```.

## For Further Reading

### [AWS F1 FPGA Developer's Kit](https://github.com/aws/aws-fpga)
### [AWS F1 Shell Interface Specification](https://github.com/aws/aws-fpga/blob/master/hdk/docs/AWS_Shell_Interface_Specification.md)

### [Using the PCIM Interface Application Note](https://github.com/awslabs/aws-fpga-app-notes/tree/master/Using-PCIM-Port)

### [How To Write Linux PCI Drivers](https://www.kernel.org/doc/Documentation/PCI/pci.txt)

## Revision History

|     Date      | Version |     Revision    |   Shell    |   Developer   |
| ------------- |  :---:  | --------------- |   :---:    |     :---:     |
| Aug. 21, 2017 |   1.0   | Initial Release | 0x071417d3 | W. Washington |

