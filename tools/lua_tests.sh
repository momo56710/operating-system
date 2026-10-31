#!/bin/bash
# Run Lua's official test suite inside LeOS. Needs `make iso` first, and network access the first time
# (the test files are downloaded from lua.org, not stored in this repository).
#   tools/lua_tests.sh            run all the files
#   tools/lua_tests.sh strings math   run only these
set -e
cd "$(dirname "$0")/.."
WORK=build/lua-tests
URL=https://www.lua.org/tests/lua-5.4.7-tests.tar.gz
mkdir -p $WORK/disk/tests
if [ ! -f $WORK/lua-tests.tar.gz ]; then
    curl -fsSL -o $WORK/lua-tests.tar.gz $URL
    echo "downloaded $URL"
    sha256sum $WORK/lua-tests.tar.gz
fi
rm -rf $WORK/disk/tests && mkdir -p $WORK/disk/tests
tar xzf $WORK/lua-tests.tar.gz -C $WORK/disk/tests --strip-components=1
rm -rf $WORK/disk/tests/ltests                         # needs Lua built with its internal test library
cp tools/lua-runtests.lua $WORK/disk/tests/runtests.lua
python3 tools/mkfat32.py $WORK/disk.img 64 $WORK/disk >/dev/null
python3 tools/leos_drive.py $WORK/run.log 1 --disk $WORK/disk.img -- "cd /disk/tests" "lua runtests.lua $*" "<UNTIL:ALL TESTS FINISHED:1700>" >/dev/null 2>&1 || true
tr -d '\r' < $WORK/run.log | grep -a "RESULT\|ALL TESTS FINISHED"
