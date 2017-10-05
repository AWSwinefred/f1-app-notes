
## F1 FPGA Application Note
# How to Use Write Combining to Improve PCIe Bus Performance
## Version 1.0

## Introduction
A developer has multiple ways to transfer data to a F1 accelerator. For large transfers (>1K), either the SH DMA or custom PCIM logic are good choices for transferring data from host memory to accelerator. For small transfers, the overhead of setting up a hardware-based data move is significant and may consume more time than simply writing the data directly to the accelerator. 
Write combining (WC) is a technique used to increase host write performance to non-cacheable PCIe devices. This application note describes when to use WC and how to take advantage of WC in software for a F1 accelerator. Write bandwidth benchmarks are included to show the performance improvements possible with WC.

## Concepts
The host’s 64-bit address map in a host is subdivided into regions. These regions have various attributes assigned to them by the hypervisor and operating system to control how a user program interacts with system memory and devices.

This app note is focuses on two attributes: Non-Cacheable and Write-Combine. Data written to non-cacheable regions are not stored in a CPU’s cache to be written later (called WriteBack), but are written directly to the memory or device. For example, if a program writes a 32-bit value to a device mapped in a non-cacheable region, then the device will receive four data bytes. The hardware will generate all the appropriate strobes, masks, and shifts to ensure the bytes are placed on the correct byte lanes with the correct strobes. Depending on the bus hierarchies and protocols, single data accesses can be very slow (<< 1 GB/s), because they use only a portion of the available data bus capacity.

Using a region marked with the WC attribute can improve performance. Writes to a WC region will accumulate in a 64 byte. Once the buffer is full or a flush event occurs, a “combined” write to the device is performed. WC increases bus utilization, which results in higher performance.

## Accessing the AppPF Bar 4 Region
The F1 Developer’s Kit includes a FPGA library that can be used access a F1 card. To run this example, launch an F1 instance, clone the aws-fpga Github repository, and download the latest [app note files].

After sourcing the ./sdk_setup.sh file, use the fpga-load-local-image command to program the FPGA with the CL_DRAM_DMA AFI. (If you are running on a 16xL, program the FPGA in slot 0.)

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

Region 4 is where the card’s 64 GB of DDR memory is located. To gain access to this region, the memory must be mapped into the user address space of the application. The function, fpga_pci_attach, performs this operation and stores the information in a structure.
```
rc = fpga_pci_attach(slot_id, pf_id, bar_id, write_combine, &pci_bar_handle);
```
Four input arguments are necessary: (1) the slot number, (2) the physical function, (3) the bar/region, and (4) the write combining flag. The function uses these arguments to open the appropriate sysfs file. For example, calling the function with the following arguments, 
```
rc = fpga_pci_attach(0, 0, 4, BURST_CAPABLE, &pci_bar_handle);
```
opens the sysfs file: /sys/bus/pci/devices/0000:00:0f.0/resource4_wc and uses mmap to create a user space pointer to Region 4 with a WC attribute. The returned pci_bar_handle structure is used by other FPGA library calls to read and write the F1 card.

## Write Performance
This app note includes a program called wc_perf. To build the program run make in the directory. This program will perform various write operations with and without WC enabled based on the options used. To see a list of the available options, type wc_perf -h.

![WC Performance Graph](./Write-Combine-Performance.png)

| size | burst | burst-wc | custom | custom-wc (GB/s) |
| ---- | :---: | :---: | :---: | :---: |
| 64 | 0.003485 | 0.025869 | 0.005318 | 0.006231 |
| 128 |	0.028432 | 0.438356 | 0.054054 | 0.465455 |
| 256 |	0.010424 | 0.870748 | 0.014607| 1.117904 |
| 512 |	0.029879 | 1.16895 | 0.174863 | 2.994152 |
| 1024 | 0.030233 | 1.376344 | 0.207245 | 4.830189 |
| 2048 | 0.030331 | 1.530643 | 0.007623 | 3.871456 |
| 4096 | 0.029984 | 1.165955 | 0.00727 | 2.055193 |
| 8192 | 0.030147 | 1.304874 | 0.007375 | 3.153195 |
| 16384 | 0.029303 | 1.40286 | 0.007247 | 3.735522 |
| 32768 | 0.028774 | 1.400402 |	0.007296 | 2.68987 |
| 65536 | 0.028624 | 1.406503 |	0.007218 | 1.984857 |
| 131072 | 0.029077 | 1.407107 | 0.007273 | 1.969438 |
| 262144 | 0.02878 | 1.383455 | 0.007274 | 1.942484 |
| 524288 | 0.028753 | 1.356239 | 0.007272 | 1.921264 |
| 1048576 | 0.028933 | 1.334822 | 0.007254 | 1.893789 |
| 2097152 | 0.028839 | 1.513256 | 0.007259 | 1.877147 |
|4194304 | 0.02885 | 1.472497 | 0.00726 | 1.872085 |


## A Few Words about Side Effects

User vs Physical Space
PCIS Interface
DDR Counts

Appendix

Application Note:


## For Further Reading:
The sysfs Filesystem

* https://www.kernel.org/pub/linux/kernel/people/mochel/doc/papers/ols-2005/mochel.pdf
* https://www.kernel.org/doc/Documentation/filesystems/sysfs.txt
* https://www.kernel.org/doc/Documentation/PCI/pci.txt

## Revision History

|     Date      | Version |     Revision    |   Shell    |   Developer   |
| ------------- |  :---:  | --------------- |   :---:    |     :---:     |
| Oct. 5, 2017  |   1.0   | Initial Release | 0x071417d3 | W. Washington |
