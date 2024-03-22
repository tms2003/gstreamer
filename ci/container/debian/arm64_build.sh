#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export LLVM_VERSION="${LLVM_VERSION:=15}"

apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*
apt-get update

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    libssl-dev
)

DEPS=(
    autoconf
    automake
    bc
    bison
    ccache
    cmake
    curl
    flex
    g++
    git
    kmod
    libglib2.0-dev
    libgudev-1.0-dev
    libudev-dev
    "llvm-${LLVM_VERSION}-dev"
    ninja-build
    openssh-server
    pkgconf
    python3-mako
    python3-pil
    python3-pip
    python3-requests
    python3-setuptools
    xz-utils
    zlib1g-dev
    zstd
)

apt-get -y install "${DEPS[@]}" "${EPHEMERAL[@]}"

pip3 install --break-system-packages git+http://gitlab.freedesktop.org/freedesktop/ci-templates@ffe4d1b10aab7534489f0c4bbc4c5899df17d3f2

# Bookworm comes with meson 1.0.1, but gstreamer needs at least 1.1
pip3 install --break-system-packages 'meson==1.2.3'

GIT_BRANCH=$CI_COMMIT_REF_NAME GIT_URL=$CI_REPOSITORY_URL bash ci/docker/fedora/create-subprojects-cache.sh

. ci/container/container_pre_build.sh

apt-get purge -y "${EPHEMERAL[@]}"

. ci/container/container_post_build.sh
