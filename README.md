# FOX - A tool for testing Open-Channel SSDs 

FOX is a tool for testing Open-Channel SSDs. Developed at IT-University of Copenhagen, FOX was built for the Dragon Fire Card (https://github.com/DFC-OpenSource) and OX Controller (https://github.com/DFC-OpenSource/ox-ctrl) evaluation, but once liblightnvm (http://lightnvm.io/liblightnvm/) is properly installed, any Open-Channel SSD can be tested by FOX. Several options and multi-thread I/Os allows a wide range of workloads, stressing the open-channel SSD as much as possible.

Fast examples:
```
sudo ./fox
sudo ./fox run -d /dev/nvme0n1 -j 8 -c 8 -l 4 -b 1 -p 128 -w 50 -o -m -e 1
sudo ./fox run -d /dev/nvme0n1 -j 8 -c 8 -l 4 -b 1 -p 128 -v 64 -w 100 -o -e 3
sudo ./fox run -d /dev/nvme0n1 -j 1 -c 8 -p 128 -r 70 -o -m -e 2
```

You can find information on how to set up the environment looking at the OX and OX-QEMU repositories:
```
https://github.com/DFC-OpenSource/ox-ctrl
https://github.com/DFC-OpenSource/qemu-ox
```
You can also find information about other devices and QEMU implementations looking at:
```
http://lightnvm.io/
```

# Concepts

- Workload: A set of parameters that defines the experiment behavior. Check 'struct fox_workload'.
 
- Job/node: Thread that carries a distribution and performs iterations.
 
- Distribution: Set of blocks distributed among the units of parallelism channels and LUNs) assigned to a node.
 
- Iteration: Group of r/w operations in a given distribution. The iteration is finished when all the pages in the distribution are
  programmed. In a 100% read workload, the iteration finishes when all the pages have been read.
 
- Row: Group of pages where each page is located in a different unit of parallelism. Each row is composed by 'n' pages, where 'n' is the maximum available units given to a specific node.
 
- Column: Page offset within a row. 

- Engine: Specific way for I/O scheduling. It defines the node I/O sequence and how the iteration will be performed per node. 

Example: 2 Channels. 2 LUNS per channel. 'nb' blocks. 'np' pages.
``` 
  (Channel,LUN,block,page)
 
           col      col      col      col
  row   (0,0,0,0)(1,0,0,0)(0,1,0,0)(1,1,0,0)
  row   (0,0,0,1)(1,0,0,1)(0,1,0,1)(1,1,0,1)
  row   (0,0,0,2)(1,0,0,2)(0,1,0,2)(1,1,0,2)
  row   (0,0,1,0)(1,0,1,0)(0,1,1,0)(1,1,1,0)
  row   (0,0,1,1)(1,0,1,1)(0,1,1,1)(1,1,1,1)
  row   (0,0,1,2)(1,0,1,2)(0,1,1,2)(1,1,1,2)
                           ...
  row   (0,0,nb,np)(0,1,nb,np)(1,0,nb,np)(1,1,nb,np)
```
# Engine 1: All sequential. 

I/Os are submitted from page 0 to page 'n' sequentially within a block. LUNS and channels are picked sequentially within the geometry given to a specific thread.
```
 #IO-sequence(Channel,LUN,block,page)
 
              col          col          col          col
  row   #01(0,0,0,0) #07(1,0,0,0) #13(0,1,0,0) #19(1,1,0,0)
  row   #02(0,0,0,1) #08(1,0,0,1) #14(0,1,0,1) #20(1,1,0,1)
  row   #03(0,0,0,2) #09(1,0,0,2) #15(0,1,0,2) #21(1,1,0,2)
  row   #04(0,0,1,0) #10(1,0,1,0) #16(0,1,1,0) #22(1,1,1,0)
  row   #05(0,0,1,1) #11(1,0,1,1) #17(0,1,1,1) #23(1,1,1,1)
  row   #06(0,0,1,2) #12(1,0,1,2) #18(0,1,1,2) #24(1,1,1,2)
```

 # Engine 2: All round-robin. (recommended for performance)
 
 I/Os are submitted as round-robin in the columns. Pages, Blocks, LUNs and channels are picked as round-robin:   
``` 
 #IO-sequence(Channel,LUN,block,page)
 
              col          col          col          col
  row   #01(0,0,0,0) #02(1,0,0,0) #03(0,1,0,0) #04(1,1,0,0)
  row   #09(0,0,0,1) #10(1,0,0,1) #11(0,1,0,1) #12(1,1,0,1)
  row   #17(0,0,0,2) #18(1,0,0,2) #19(0,1,0,2) #20(1,1,0,2)
  row   #05(0,0,1,0) #06(1,0,1,0) #07(0,1,1,0) #08(1,1,1,0)
  row   #13(0,0,1,1) #14(1,0,1,1) #15(0,1,1,1) #16(1,1,1,1)
  row   #21(0,0,1,2) #22(1,0,1,2) #23(0,1,1,2) #24(1,1,1,2)
```

# Engine 3: I/O Isolation.

Each node will perform only 1 operation type (read/write). This allows different tenants (threads) to perform an isolated iteration in their given units of parallelism. Nodes will perform the iterations in parallel.

All iterations will be performed as round-robin, described in Engine 2.

The number of threads performing reads and writes are defined based on the -r and -w parameters. 
``` 
-j 10 -r 70 -w 30 : 7 READ jobs, 3 WRITE jobs
-j 10 -r 100      : 10 READ jobs, 0 WRITE jobs
-j 10 -w 50       : 5 READ jobs, 5 WRITE jobs
```

FOX run parameters:
```
lab@lab:~/fox$ ./fox run --help
Usage: fox run [OPTION...]

Use this command to run FOX based on parameters by the command line.

 Example:
     fox run <parameters>

If parameters are not provided, the default values will be used:
     device   = /dev/nvme0n1
     runtime  = 0 (only 1 iteration will be performed)
     channels = 1
     luns     = 1
     blocks   = 1
     pages    = 1
     jobs     = 1
     read     = 100
     write    = 0
     vector   = 1 page = <sectors per page * number of planes>
     sleep    = 0
     memcmp   = disabled
     output   = disabled
     engine   = 1 (sequential)

  -b, --blocks=<int>         Number of blocks per LUN.
  
  -c, --channels=<int>       Number of channels.
  
  -d, --device=<char>        Device name. e.g: /dev/nvme0n1
  
  -e, --engine=<int>         I/O engine ID. (1)sequential, (2)round-robin,
                             (3)isolation. Please check documentation for
                             detailed information.
                             
  -j, --jobs=<int>           Number of jobs. Jobs are executed in parallel and
                             the geometry of the device is split among threaded
                             jobs.
                             
  -l, --luns=<int>           Number of LUNs per channel.
  
  -m, --memcmp               If present, it enables buffer comparison between
                             write and read buffers. Not all cases are suitable
                             for memory comparison. Cases not supported: 100%
                             reads, Engine 3 (isolation).
                             
  -o, --output               If present, a set of output files will be
                             generated. For now .csv is supported. Files created 
                             under ./output folder:
                              - timestamp_fox_meta.csv -> Metadata including the workload
                              parameters and the final results.
                              - timestamp_fox_io.csv -> Per IO information:
                                sequence;node_sequence;node_id;channel;lun;block;page;
                                start;end;latency;type;is_failed;read_memcmp;bytes
                              - timestamp_fox_rt.csv -> Per thread realtime information 
                              (throughtput and IOPS). There is an entry each half second.
                             
  -p, --pages=<int>          Number of pages per block.
  
  -r, --read=<0-100>         Percentage of read. Read+write must sum 100.
  -s, --sleep=<int>          Maximum delay between I/Os. Jobs sleep between
                             I/Os in a maximum of <sleep> u-seconds.
                             Each thread gets a different sleep time (smaller
                             than <sleep>.
                             
  -t, --runtime=<int>        Runtime in seconds. If 0 or not present, the
                             workload will finish when all pages are done in a
                             given geometry.
                             
  -v, --vector=<int>         Number of physical sectors per I/O. This value
                             must be multiple of <sectors per page * number of
                             planes>. e.g: if device has 4 sectors per page and
                             2 planes, this value must be multiple of 8. The
                             maximum value is the device maximum sectors per
                             I/O.
                             Fox will create multi-page IOs when a sequence of 
                             pages in the same block and same LUN is requested.
                             
  -w, --write=<0-100>        Percentage of write. Read+write must sum 100.
  
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.

Report bugs to Ivan L. Picoli <ivpi@itu.dk>.
```

FOX help:
```
lab@lab:~/fox$ ./fox --help
Usage: fox [OPTION...] fox [<cmd> [cmd-options]]

*** FOX v1.0 ***
 
 A tool for testing Open-Channel SSDs

 Available commands:
  run              Run FOX based on command line parameters.

 Examples:
  fox run <parameters>     - custom configuration
  fox --help               - show available parameters
  fox <without parameters> - run with default configuration
 
 Initial release developed by Ivan L. Picoli, <ivpi@itu.dk>


  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version

Report bugs to Ivan L. Picoli <ivpi@itu.dk>.
```

# Statistics:

  If -o option is enabled, FOX will generate output files under ./output:
```
   - timestamp_fox_meta.csv -> Metadata including the workload parameters and the final results.
   - timestamp_fox_io.csv -> Per IO information:
        sequence;node_sequence;node_id;channel;lun;block;page;start;end;latency;type;is_failed;read_memcmp;bytes
   - timestamp_fox_rt.csv -> Per thread realtime information (throughtput and IOPS). There is an entry each half second.
```
  After the execution you should get a screen like this (included in the meta CSV output file):
```
--- WORKLOAD ---

 - Device       : /dev/nvme0n1
 - Runtime      : 1 iteration
 - Num of jobs  : 1
 - N of Channels: 8
 - LUNs per Chan: 1
 - Blks per LUN : 8
 - Pgs per Blk  : 512
 - Write factor : 50 %
 - Read factor  : 50 %
 - Vector PPAs  : 8
 - Max I/O delay: 0 u-sec
 - Output file  : enabled
 - Read compare : enabled
 - Engine       : 2 (round-robin)

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
