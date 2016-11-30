#!/bin/bash
sudo ./fox nvme0n1 runtime 0 ch 8 lun 1 blk 10 pg 128 node 8 read 50 write 50 delay 1600 compare 1
