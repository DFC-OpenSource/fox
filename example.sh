#!/bin/bash
sudo ./fox nvme0n1 runtime 0 ch 8 lun 2 blk 5 pg 128 node 8 read 50 write 50 delay 0 compare 1 engine 2