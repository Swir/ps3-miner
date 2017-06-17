#!/bin/sh
##
##
##
##

export CELLSDK=/mnt/opt/projects/ps3/cell
export BUILD_PATH=/mnt/opt/projects/ps3/build

export PATH=${CELLSDK}/host-linux/bin:${CELLSDK}/host-linux/ppu/bin/:${CELLSDK}/host-linux/spu/bin/:${PATH}

export CROSS_COMPILE=ppu-lv2-

export AR=ppu-lv2-ar
export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export STRIP=${CROSS_COMPILE}strip
export RANLIB=${CROSS_COMPILE}ranlib
export AS=${CROSS_COMPILE}as
export OBJDUMP=${CROSS_COMPILE}objdump
export READELF=${CROSS_COMPILE}readelf
export STRINGS=${CROSS_COMPILE}strings
export OBJCOPY=${CROSS_COMPILE}objcopy
export ADDR2LINE=${CROSS_COMPILE}addr2line
export LD=${CROSS_COMPILE}ld
export NM=${CROSS_COMPILE}nm
export SIZE=${CROSS_COMPILE}size

## default LDFLAGS / CFLAGS with PPU
LDFLAGS="-L${CELLSDK}/host-linux/lib -L${CELLSDK}/target/ppu/lib"

## PPU SPECIFICS
BIN_PPU="${CELLSDK}/host-linux/ppu/bin"
LDFLAGS_PPU="-L${CELLSDK}/host-linux/ppu/lib -L${CELLSDK}/target/ppu/lib"
CFLAGS_PPU="-I${CELLSDK}/host-linux/ppu/include -I${CELLSDK}/target/ppu/include"

## PPU SPECIFICS
BIN_SPU="${CELLSDK}/host-linux/spu/bin"
LDFLAGS_SPU="-L${CELLSDK}/host-linux/spu/lib -L${CELLSDK}/target/spu/lib"
CFLAGS_SPU="-I${CELLSDK}/host-linux/spu/include -I${CELLSDK}/target/spu/include"

export ac_cv_func_malloc_0_nonnull=yes
export ac_cv_func_realloc_0_nonnull=yes

echo '
     ./configure --prefix="${BUILD_PATH}" --host="powerpc64-ps3-elf" --enable-static --disable-shared
'