#!/bin/env bash

if [ ! $# -eq 4 ] ; then
    echo "Help: $0 <cross_compile> <arch> <abi> <install>"
    echo "eg. $0 riscv64-unknown-linux-gnu rv64imafdc lp64d install"
    exit 1
fi

CROSS_COMPILE=$1
ARCH=$2
ABI=$3
INSTDIR=$4

STATIC_BUILD=${STATIC_BUILD:-1}

SCRIPTDIR=$(dirname $(readlink -f $BASH_SOURCE))
SCRIPTDIR=$(readlink -f $SCRIPTDIR)

ARCHABI_FLAGS="-march=$ARCH -mabi=$ABI"

function create_env {
    local instdir=$1

    echo "Generate perf environment script to $instdir/env.sh"
    touch $instdir/env.sh
    echo "SCRIPTDIR=\$(dirname \$(readlink -f \$BASH_SOURCE))" >> $instdir/env.sh
    echo "SCRIPTDIR=\$(readlink -f \$SCRIPTDIR)" >> $instdir/env.sh
    echo "export PATH=\$SCRIPTDIR/bin/:\$PATH"  >> $instdir/env.sh
    echo "export LD_LIBRARY_PATH=\$SCRIPTDIR/lib/:\$LD_LIBRARY_PATH"  >> $instdir/env.sh
}

function build_perf {
    local srcdir=$1
    local instdir=$2
    echo "Build Linux Tool Perf"
    pushd $srcdir
    local srctree=$(readlink -f $(pwd)/../../)

    if [ "x${STATIC_BUILD}" == "x1" ] ; then
        EXT_LDFLAGS="-static "
    fi
    make srctree=$srctree ARCH=riscv CROSS_COMPILE=${CROSS_COMPILE}- LDFLAGS="${ARCHABI_FLAGS} -L${instdir}/lib -lz" EXTRA_CFLAGS="${ARCHABI_FLAGS} -I${instdir}/include" CXXFLAGS="${ARCHABI_FLAGS} -I${instdir}/include"  DESTDIR=$instdir clean
    # need extra ldflags -lz to check libelf
    make srctree=$srctree ARCH=riscv CROSS_COMPILE=${CROSS_COMPILE}- LDFLAGS="${EXT_LDFLAGS} ${ARCHABI_FLAGS} -L${instdir}/lib -lz" EXTRA_CFLAGS="${ARCHABI_FLAGS} -I${instdir}/include" CXXFLAGS="${ARCHABI_FLAGS} -I${instdir}/include"  DESTDIR=$instdir install
    popd
}

pushd $SCRIPTDIR

if [ ! -d ${INSTDIR} ] ; then
    echo "${INSTDIR} not exist! create it now!"
    mkdir -p $INSTDIR
fi

INSTDIR=$(readlink -f $INSTDIR)

if [[ "$ARCH" == *"rv32"* ]] ; then
    echo "Error: Linux Perf is not yet supported in rv32 linux"
    exit 1
fi

build_perf $(pwd) $INSTDIR
create_env $INSTDIR

popd

exit 0
