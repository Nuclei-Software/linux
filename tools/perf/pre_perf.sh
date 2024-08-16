#!/bin/env bash

if [ ! $# -eq 4 ] ; then
    echo "Help: $0 <cross_compile> <arch> <abi> <install>"
    echo "eg. $0 riscv64-unknown-linux-gnu rv64imafdc lp64d install"
    exit 1
fi

CROSS_COMPILE=$1
ARCH=$2
ABI=$3
LIB_INSTDIR=$4


SCRIPTDIR=$(dirname $(readlink -f $BASH_SOURCE))
SCRIPTDIR=$(readlink -f $SCRIPTDIR)

ZLIB_SRCDIR=$(readlink -f $SCRIPTDIR/../../../zlib)
ELFUTILS_SRCDIR=$(readlink -f $SCRIPTDIR/../../../elfutils)
LIBTRACEEVENT_SRCDIR=$(readlink -f $SCRIPTDIR/../../../libtraceevent)

ARCHABI_FLAGS="-march=$ARCH -mabi=$ABI"

function build_zlib {
    local srcdir=$1
    local instdir=$2
    echo "Build ZLib"
    pushd ${srcdir}
    rm -rf install
    make ARCH_ABI="${ARCHABI_FLAGS}" clean
    make ARCH_ABI="${ARCHABI_FLAGS}" -j install
    echo "Copy built zlib to ${instdir}"
    cp -rf install/* ${instdir}/
    popd
}

function build_elfutils {
    local srcdir=$1
    local instdir=$2
    echo "Build elfutils"
    pushd $srcdir
    autoreconf -i -f
    rm -rf install
    # --disable-demangler is required for rv32
    # CFLAGS/CXXFLAGS/CPPFLAGS/LDFLAGS need to passed after ./configure, not passed before ./configure
    ./configure --host=${CROSS_COMPILE} --prefix=$(pwd)/install --disable-libdebuginfod --enable-maintainer-mode  --disable-debuginfod --disable-demangler CFLAGS="${ARCHABI_FLAGS}" CXXFLAGS="${ARCHABI_FLAGS}" LDFLAGS="${ARCHABI_FLAGS} -L${instdir}/lib" CPPFLAGS="-I${instdir}/include"
    make clean
    make -j install
    cp -rf install/* ${instdir}/
    popd
}

function build_libtraceevent {
    local srcdir=$1
    local instdir=$2
    echo "Build libtraceevent"
    pushd $srcdir
    rm -rf install
    make CROSS_COMPILE=${CROSS_COMPILE}- CONFIG_FLAGS="${ARCHABI_FLAGS}" LDFLAGS="${ARCHABI_FLAGS}" DESTDIR=$(pwd)/install clean
    make CROSS_COMPILE=${CROSS_COMPILE}- CONFIG_FLAGS="${ARCHABI_FLAGS}" LDFLAGS="${ARCHABI_FLAGS}" DESTDIR=$(pwd)/install libdir_relative=lib prefix= pkgconfig_dir=/lib/pkgconfig -j install
    cp -rf install/* ${instdir}/
    popd
}

pushd $SCRIPTDIR

if [ -d ${LIB_INSTDIR} ] ; then
    echo "${LIB_INSTDIR} already exist! remove it now!"
    rm -rf $LIB_INSTDIR
fi

mkdir -p $LIB_INSTDIR

LIB_INSTDIR=$(readlink -f $LIB_INSTDIR)

build_zlib $ZLIB_SRCDIR $LIB_INSTDIR
build_elfutils $ELFUTILS_SRCDIR $LIB_INSTDIR
build_libtraceevent $LIBTRACEEVENT_SRCDIR $LIB_INSTDIR

popd

echo "Please find prebuilt zlib, libelf and libtraceevent in $LIB_INSTDIR"

exit 0
