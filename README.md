# FOX - A tool for testing Open-Channel SSDs 

FOX is a tool to test Open-Channel SSDs with direct PPA IOs through liblightnvm. Several options and multithread IOs can be modified in order to generate a wide range of workloads. After setting all the environment in QEMU or a real Open-Channel device, please run the 'example.sh' file to try FOX.

You can find information on how to set the environment in the OX and OX-QEMU repositories:
```
https://github.com/DFC-OpenSource/ox-ctrl
https://github.com/DFC-OpenSource/qemu-ox
```

Options:

For now, a very poor argument parser is implemented, sorry for that. Contributions are welcome.

```
Usage:
$ sudo ./fox <string> runtime <int> ch <int> lun <int> blk <int> pg <int> node <int> read <int> write <int> delay <int> compare <int>

Example:
$ sudo ./fox nvme0n1 runtime 0 ch 8 lun 1 blk 10 pg 128 node 8 read 50 write 50 delay 1600 compare 1

IMPORTANT: All the parameters must be included and exactly in this order, we did not implement an 'argp.h' parser due a lack of time. Contributions are welcome.
```
The first parameter is the device name (nvme0n1 for instance).

#runtime
If 0, the workload will be executed only once.
If positive, the workload will run for <int> seconds.

#ch
The number of channels included in the workload.

#lun
The number of luns per channel included in the workload.

#blk
The number of blocks per lun included in the workload

#pg
The number of pages per block included in the workload.

#node
The number of threads which the LUNs and channels will be splited among.

#read
The percentage of read operations in the workload. read and write must sun 100 %.

#write
The percentage of write operations in the workload. read and write must sun 100 %.

#delay
If 0, no delay is included. 
If positive, the <int> u-seconds will be the maximum delay between the IOs, each thread receives a smaller value for the delay. Each thread will get a different delay in order to mix the IO times.

#compare
If 0, no data comparison will be performed.
If 1, after every read operation, a comparison between the previous written and the current read data will be performed.

#Statistics:
In the end you should get a screen like this:
```
--- WORKLOAD ---

 - Threads      : 8
 - N of Channels: 8
 - LUNs per Chan: 1
 - Blks per LUN : 10
 - Pgs per Blk  : 128
 - Write factor : 50 %
 - read factor  : 50 %

 --- GEOMETRY DISTRIBUTION [TID: (CH LUN)] ---

 [0: (0 0)]   [1: (1 0)]   [2: (2 0)]   [3: (3 0)]
 [4: (4 0)]   [5: (5 0)]   [6: (6 0)]   [7: (7 0)]

 - Preparing blocks... [80/80]

 - Synchronizing threads...

 - Workload started.

 [0:100%] [1:100%] [2:100%] [3:100%] [4:100%] [5:100%] [6:100%] [7:100%] [100%|91.27 MB/s]

 --- RESULTS ---

 - Total time    : 5707 m-sec
 - Read data     : 327680 KB
 - Read pages    : 10240
 - Written data  : 327680 KB
 - Written pages : 10240
 - Throughput    : 112.14 MB/sec
 - IOPS          : 3588.4
 - Erased blocks : 80
 - Erase latency : 3990 u-sec
 - Read latency  : 1153 u-sec
 - Write latency : 1338 u-sec
 - Failed memcmp : 0
 - Failed writes : 0
 - Failed reads  : 0
 - Failed erases : 0
 ```
