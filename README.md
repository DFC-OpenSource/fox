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
$ sudo ./fox <string> runtime <int> ch <int> lun <int> blk <int> pg <int> node <int> read <int> write <int> delay <int> compare <int> output <int> engine <int>

Example:
$ sudo ./fox nvme0n1 runtime 0 ch 8 lun 1 blk 10 pg 128 node 8 read 50 write 50 delay 0 compare 1 output 1 engine 2

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

#output
If 0, no file is created
If 1, files in .csv format are created under ./output folder. Each file contains:
  - timestamp_fox_meta.csv -> Metadata including the workload parameters and the final results
  - timestamp_fox_io.csv -> Per IO information. The .csv is fields:
     sequence;node_sequence;node_id;channel;lun;block;page;start;end;latency;type;is_failed;read_memcmp;bytes
  - timestamp_fox_rt.csv -> Per thread realtime information (throughtput and IOPS). There is an entry each half second.
  
#engine
It is current implemented 2 IO engines in fox:

 - Engine 1: All sequential. 
   IOs are submitted from page 0 to page n sequentially within a block and LUNS/channels are picked sequentially within the address space given to a specific thread.
   
 - Engine 2: All round-robin. (recommended for performance)
   IOs are submitted as round-robin in the columns, following this rules:   
``` 
    -> A workload is a set of parameters that defines the experiment behavior. Check 'struct fox_workload'.
 
    -> A node is a thread that carries a distribution and performs iterations.
 
    -> A distribution is a set of blocks distributed among the units of parallelism
     (channels and LUNs) assigned to a node.
 
    -> An iteration is a group of r/w operations in a given distribution.
  The iteration is finished when all the pages in the distribution are
  programmed. In a 100% read workload, the iteration finishes when all the
  pages have been read.
 
    -> A row is a group of pages where each page is located in a different unit
  of parallelism. Each row is composed by n pages, where n is the maximum
  available units given to a specific node.
 
    -> A column is the page offset within a row. For instance:
 
  (CH=2,LUN=2,BLK=nb,PG=np)
 
              col      col      col      col
  row ->   (0,0,0,0)(0,1,0,0)(1,0,0,0)(1,1,0,0)
  row ->   (0,0,0,1)(0,1,0,1)(1,0,0,1)(1,1,0,1)
  row ->   (0,0,0,2)(0,1,0,2)(1,0,0,2)(1,1,0,2)
                           ...
  row ->   (0,0,nb,np)(0,1,nb,np)(1,0,nb,np)(1,1,nb,np)
```
#Statistics:
In the end you should get a screen like this (included in the meta CSV output file):
```
--- WORKLOAD ---

 - Device       : nvme0n1
 - Runtime      : 1 iteration
 - Threads      : 8
 - N of Channels: 8
 - LUNs per Chan: 1
 - Blks per LUN : 10
 - Pgs per Blk  : 128
 - Write factor : 50 %
 - read factor  : 50 %
 - Max I/O delay: 0 u-sec
 - Engine       : 0x2
 - Read compare : y

 --- GEOMETRY DISTRIBUTION [TID: (CH LUN)] ---

 [0: (0 0)]   [1: (1 0)]   [2: (2 0)]   [3: (3 0)]
 [4: (4 0)]   [5: (5 0)]   [6: (6 0)]   [7: (7 0)]

 - Preparing blocks... [80/80]

 - Synchronizing threads...

 - Workload started.

 [0:100%] [1:100%] [2:100%] [3:100%] [4:100%] [5:100%] [6:100%] [7:100%] [100%|91.27 MB/s|3524.1]

 --- RESULTS ---

 - Elapsed time  : 5707 m-sec
 - I/O time (sum): 40524 m-sec
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
