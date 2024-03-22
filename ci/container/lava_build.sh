#!/usr/bin/env bash
# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2034 # Variables are used in scripts called from here
# shellcheck disable=SC2086 # we want word splitting
# When changing this file, you need to bump the following
# .gitlab-image-tags.yml tags:
# KERNEL_ROOTFS_TAG
# If you need to update the fluster vectors cache without updating the fluster revision,
# you can update the FLUSTER_VECTORS_VERSION tag in ci/image-tags.yml.
# When changing FLUSTER_REVISION, KERNEL_ROOTFS_TAG needs to be updated as well to rebuild
# the rootfs.

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive
export LLVM_VERSION="${LLVM_VERSION:=15}"

KERNEL_IMAGE_BASE=${KERNEL_IMAGE_BASE_MAINLINE}
if [ "$CI_PROJECT_PATH" != "$FDO_UPSTREAM_REPO" ]; then
    if ! curl -s -X HEAD -L --retry 4 -f --retry-delay 60 \
      "${KERNEL_IMAGE_BASE}/done"; then
	echo "Using kernel from the fork, cached from mainline is unavailable."
	KERNEL_IMAGE_BASE=${KERNEL_IMAGE_BASE_FORK}
    else
	echo "Using the cached mainline kernel"
    fi
fi

check_minio()
{
    S3_PATH="${S3_HOST}/gst-rootfs/$1/${DISTRIBUTION_TAG}/${DEBIAN_ARCH}"
    if curl -L --retry 4 -f --retry-delay 60 -s -X HEAD \
      "https://${S3_PATH}/done"; then
        echo "Remote files are up-to-date, skip rebuilding them."
        exit
    fi
}

check_kernel()
{
    if curl -L --retry 4 -f --retry-delay 60 -s -X HEAD \
      "${KERNEL_IMAGE_BASE}/done"; then
        echo "Kernel is up to date, skip building"
        SKIP_KERNEL_BUILD=1
    fi
}

# TODO: Reactivate once S3 access is available
check_minio "${FDO_UPSTREAM_REPO}"
check_minio "${CI_PROJECT_PATH}"

#echo "Hello world" > test.txt
#S3_PATH="${S3_HOST}/gst-rootfs/${CI_PROJECT_PATH}/${DISTRIBUTION_TAG}/${DEBIAN_ARCH}"
#ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" test.txt https://${S3_PATH}/test.txt

#exit 1

check_kernel

. ci/container/container_pre_build.sh

# Install rust, which we'll be using for deqp-runner.  It will be cleaned up at the end.
. ci/container/build-rust.sh

KERNEL_ARCH="arm64"
DEVICE_TREES="mt8195-cherry-tomato-r2.dtb"
KERNEL_IMAGE_NAME="Image.gz"

# no need to remove these at end, image isn't saved at the end
# Change these to what is actually built
CONTAINER_EPHEMERAL=(
    automake
    build-essential
    bc
    "clang-${LLVM_VERSION}"
    cmake
    curl
    mmdebstrap
    gcc
    git
    glslang-tools
    libdrm-dev
    libxext-dev
    libfontconfig-dev
    libgbm-dev
    libpng-dev
    libssl-dev
    libudev-dev
    libxkbcommon-dev
    libwayland-dev
    ninja-build
    openssh-server
    patch
    protobuf-compiler
    python-is-python3
    python3-distutils
    python3-mako
    python3-numpy
    python3-serial
    python3-venv
    unzip
    zstd
)

apt-get update
apt-get install -y --no-remove \
		   -o Dpkg::Options::='--force-confdef' -o Dpkg::Options::='--force-confold' \
		   "${CONTAINER_EPHEMERAL[@]}" \
                   "${CONTAINER_ARCH_PACKAGES[@]}" \
                   ${EXTRA_LOCAL_PACKAGES}

ROOTFS=/lava-files/rootfs-${DEBIAN_ARCH}
mkdir -p "$ROOTFS"

# rootfs packages
PKG_BASE=(
  tzdata mount
)
PKG_CI=(
  bash ca-certificates curl
  jq netcat-openbsd dropbear openssh-server
  git
  python3-dev python3-pip python3-setuptools python3-wheel
)
PKG_GSTREAMER_DEP=(
  libgudev-1.0-dev
)
PKG_DEP=(
  libpng16-16
  libpython3.11 python3 python3-lxml python3-mako python3-numpy python3-packaging python3-pil python3-renderdoc python3-requests python3-simplejson python3-yaml # Python
  sntp
  strace
  zstd
)
# arch dependent rootfs packages
PKG_ARCH=(
  firmware-linux-nonfree
)

mmdebstrap \
    --variant=apt \
    --arch="${DEBIAN_ARCH}" \
    --components main,contrib,non-free-firmware \
    --include "${PKG_BASE[*]} ${PKG_CI[*]} ${PKG_DEP[*]} ${PKG_GSTREAMER_DEP[*]} ${PKG_ARCH[*]}" \
    bookworm \
    "$ROOTFS/" \
    "http://deb.debian.org/debian"

############### Building
STRIP_CMD="strip"
mkdir -p $ROOTFS/usr/lib/$GCC_ARCH

############### Build dEQP runner
export DEQP_RUNNER_GIT_REV=refs/merge-requests/64/head
. ci/container/build-deqp-runner.sh
mkdir -p $ROOTFS/usr/bin
mv /usr/local/bin/*-runner $ROOTFS/usr/bin/.

############### Install fluster
git clone https://github.com/fluendo/fluster.git
pushd fluster
git checkout ${FLUSTER_REVISION}
#./fluster.py download JCT-VC-HEVC_V1 JCT-VC-SCC JCT-VC-RExt JCT-VC-MV-HEVC JVT-AVC_V1 JVT-FR-EXT VP9-TEST-VECTORS VP9-TEST-VECTORS-HIGH VP8-TEST-VECTORS VP8-TEST-VECTORS-HIGH
./fluster.py download VP8-TEST-VECTORS
popd

mv fluster $ROOTFS/opt/

############### Build kernel
if [ -z "${SKIP_KERNEL_BUILD}" ]; then
	. ci/container/build-kernel.sh ${KERNEL_IMAGE_NAME} ${DEVICE_TREES}
fi

############### Delete rust, since the tests won't be compiling anything.
rm -rf /root/.cargo
rm -rf /root/.rustup

############### Fill rootfs
cp ci/container/setup-rootfs.sh $ROOTFS/.
cp ci/container/strip-rootfs.sh $ROOTFS/.
chroot $ROOTFS bash /setup-rootfs.sh
rm "$ROOTFS/setup-rootfs.sh"
rm "$ROOTFS/strip-rootfs.sh"
cp /etc/wgetrc $ROOTFS/etc/.

ROOTFSTAR="lava-rootfs.tar.zst"
du -ah "$ROOTFS" | sort -h | tail -100
pushd $ROOTFS
  tar --zstd -cf /lava-files/${ROOTFSTAR} .
popd

. ci/container/container_post_build.sh

ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/"${ROOTFSTAR}" \
      https://${S3_PATH}/"${ROOTFSTAR}"

touch /lava-files/done
# DEBUG
#cp /lava-files/"${ROOTFSTAR}" .

ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/done https://${S3_PATH}/done
