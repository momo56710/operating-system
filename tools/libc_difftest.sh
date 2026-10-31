#!/bin/bash
# Differential test of the LeOS C library: user/progs/libctest.c is compiled for this machine (glibc) and for
# LeOS; both print the same ~1,900 lines (printf, strtod, math, strings, files, time...). This compares a CRC-32
# of the output. Needs `make iso` first.
set -e
cd "$(dirname "$0")/.."
WORK=build/libc-difftest
mkdir -p $WORK
gcc -std=gnu11 -O1 -w -o $WORK/host_libctest user/progs/libctest.c -lm
TZ=UTC $WORK/host_libctest > $WORK/host_out.txt
host=$(python3 -c "import zlib;d=open('$WORK/host_out.txt','rb').read();print('%08x %d'%(zlib.crc32(d),len(d)))")
python3 tools/leos_drive.py $WORK/run.log 2 -- "libctest > /tmp/lt.txt" "cksum /tmp/lt.txt" >/dev/null 2>&1 || true
leos=$(tr -d '\r' < $WORK/run.log | grep -a "/tmp/lt.txt$" | tail -1 | awk '{print $1, $2}')
echo "glibc: $host"
echo "LeOS:  $leos"
[ "$host" = "$leos" ] && echo "IDENTICAL" || { echo "DIFFERENT"; exit 1; }
