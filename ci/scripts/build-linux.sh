#!/bin/bash
#
# Based on the build-linux.sh script from the Mutter project:
# https://gitlab.gnome.org/GNOME/mutter/-/blob/main/src/tests/kvm/build-linux.sh
#
# Script for building the Linux kernel from git. It aims to build a kernel image
# that is suitable for running in a virtual machine and is aimed to used for
# testing.
#
# Usage: build-linux.sh [REPO-URL] [BRANCH|TAG] [OUTPUT-IMAGE] [...CONFIGS]
#
# Where [..CONFIGS] can be any number of configuration options, e.g.
# --enable CONFIG_DRM_VKMS
#

set -e
set -x

# From scripts/subarch.include in linux
function get-subarch()
{
  uname -m | sed -e s/i.86/x86/ \
                 -e s/x86_64/x86/ \
                 -e s/sun4u/sparc64/ \
                 -e s/arm.*/arm/ -e s/sa110/arm/ \
                 -e s/s390x/s390/ -e s/parisc64/parisc/ \
                 -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
                 -e s/sh[234].*/sh/ -e s/aarch64.*/arm64/ \
                 -e s/riscv.*/riscv/
}

REPO="$1"
BRANCH_OR_TAG="$2"
IMAGE="$3"

ARCH=$(uname -m)
SUBARCH=$(get-subarch)

shift
shift
shift

# Setting requested configuration
CONFIGS_ENABLE=()
CONFIGS_DISABLE=()
for config in ${KERNEL_ENABLE}; do
  CONFIGS_ENABLE+=( "$config" )
done

for config in ${KERNEL_DISABLE}; do
  CONFIGS_DISABLE+=( "$config" )
done

echo Building Linux for $ARCH \($SUBARCH\)...

if [ -d linux ]; then
  pushd linux
  git fetch --depth=1 $REPO $BRANCH_OR_TAG
  git checkout FETCH_HEAD
else
  git clone --depth=1 --branch=$BRANCH_OR_TAG $REPO linux
  pushd linux
fi

# Apply visl patches until they are upstreamed
for patch in ../ci/docker/fedora/patches/*.patch; do
	patch -p1 < "${patch}"
done

make defconfig
sync
make kvm_guest.config

if [ ! -z "${KERNEL_DISABLE}" ]; then
  echo Disabling ${CONFIGS_DISABLE[@]}...
  ./scripts/config ${CONFIGS_DISABLE[@]/#/--disable }
fi

if [ ! -z "${KERNEL_ENABLE}" ]; then
  echo Enabling ${CONFIGS_ENABLE[@]}...
  ./scripts/config ${CONFIGS_ENABLE[@]/#/--enable }
fi

make olddefconfig
make -j8 WERROR=0
make INSTALL_MOD_PATH=$(pwd)/modules_install INSTALL_MOD_STRIP=1 modules_install
make INSTALL_DTBS_PATH=$(pwd)/dtbs_install dtbs_install

popd

TARGET_DIR="$(dirname "$IMAGE")"
TARGET_NAME="$(basename "$IMAGE")"
mkdir -p "$TARGET_DIR"
mv linux/arch/$SUBARCH/boot/${TARGET_NAME} "$IMAGE"
mv linux/.config $TARGET_DIR/.config

tar --zstd -cf $TARGET_DIR/modules.tar.zstd -C linux/modules_install .
tar --zstd -cf $TARGET_DIR/dtbs.tar.zstd -C linux/dtbs_install .

rm -rf linux
