#!/bin/bash

PPD_FILE=ep_tmc600.drv
gcc -g3 -o rastertotmc600 rastertotmc600.c -lcupsimage -lcupsfilters -lcups || exit 1
ppdc ${PPD_FILE} || exit 1
export PPD=$(pwd)/ppd/$(basename ${PPD_FILE} .drv).ppd
rm -rf test
mkdir -p test
/usr/lib/cups/filter/imagetoraster 1 me '' 1 '' "$1" >test/out.ras || exit 1
./rastertotmc600 1 me '' 1 '' test/out.ras >test/out.prn || exit 1
