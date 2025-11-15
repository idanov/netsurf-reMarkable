#!/bin/bash

# Build script for NetSurf browser and libnsfb
# This script builds both libnsfb and NetSurf browser.
# All other dependencies and libraries are built in the Dockerfile.

set -e

SCRIPTPATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Validate required environment variable
if [ -z "$TARGET_WORKSPACE" ]; then
    echo "ERROR: TARGET_WORKSPACE is required, but not set."
    exit 1
fi

# Set USE_CPUS if not already set
# In Docker container, read from temp file created during build
# Otherwise calculate from number of CPUs
if [ -z "$USE_CPUS" ]; then
    if [ -f /tmp/use_cpus.txt ]; then
        USE_CPUS=$(cat /tmp/use_cpus.txt)
    else
        NCPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null || echo 1)
        NCPUS=$((NCPUS * 2))
        USE_CPUS="-j${NCPUS}"
    fi
fi

echo "Building libnsfb and NetSurf for reMarkable..."
echo "TARGET_WORKSPACE: $TARGET_WORKSPACE"
echo "PREFIX: $PREFIX"
echo "HOST: $HOST"

# Build libnsfb first
echo "Building libnsfb..."
cd $TARGET_WORKSPACE/libnsfb/

CFLAGS="-I/opt/x-tools/arm-remarkable-linux-gnueabihf/arm-remarkable-linux-gnueabihf/sysroot/usr/include/libevdev-1.0" \
LDFLAGS="-L/opt/x-tools/arm-remarkable-linux-gnueabihf/arm-remarkable-linux-gnueabihf/sysroot/usr/lib -levdev" \
${MAKE} PREFIX=${PREFIX} HOST=${HOST} $USE_CPUS install

echo "libnsfb build complete!"

# Build NetSurf for framebuffer target
echo "Building NetSurf..."
cd $TARGET_WORKSPACE/netsurf/

# libevdev and pthread are required for reMarkable input handling
export CC="arm-remarkable-linux-gnueabihf-gcc"
export STRIP="arm-remarkable-linux-gnueabihf-strip"
export LDFLAGS="$LDFLAGS -levdev -lpthread"

# Add sysroot /usr/local paths for freetype and other libraries
SYSROOT="/opt/x-tools/arm-remarkable-linux-gnueabihf/arm-remarkable-linux-gnueabihf/sysroot"
export CFLAGS="$CFLAGS -I${SYSROOT}/usr/local/include/freetype2 -I${SYSROOT}/usr/local/include"

# Adjust PKG_CONFIG to find both PREFIX libraries and sysroot libraries
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${SYSROOT}/usr/local/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${PREFIX}/lib/pkgconfig:${SYSROOT}/usr/local/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig"

${MAKE} TARGET=framebuffer \
    NETSURF_FB_FONTLIB=freetype \
    NETSURF_STRIP_BINARY=YES \
    NETSURF_USE_LIBICONV_PLUG=NO \
    NETSURF_USE_DUKTAPE=NO \
    NETSURF_REMARKABLE=YES \
    LDLIBS="-L${PREFIX}/lib -lnslog" \
    $USE_CPUS

echo "NetSurf build complete!"
