#!/bin/bash
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Based on the virtme-run.sh script from the Mutter project:
# https://gitlab.gnome.org/GNOME/mutter/-/blob/main/src/tests/kvm/virtme-run.sh
#
# Run fluster tests in a virtual machine using virtme-ng.
#
# $1: A Linux kernel image
# $2: The root build dir
# $3: The root source dir
# $4: The codec to update references

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
MESON_BUILD_DIR="$2"
MESON_SOURCE_DIR="$3"
CODEC="${4}"

decoder_to_ts() {
	case $1 in
	GStreamer-h.264-V4L2SL-Gst1.0)
		echo JVT-AVC_V1 JVT-FR-EXT
		;;
	GStreamer-h.265-V4L2SL-Gst1.0)
		echo JCT-VC-HEVC_V1 JCT-VC-SCC JCT-VC-RExt JCT-VC-MV-HEVC
		;;
	GStreamer-vp8-V4L2SL-Gst1.0)
		echo VP8-TEST-VECTORS
		;;
	GStreamer-vp9-V4L2SL-Gst1.0)
		echo VP9-TEST-VECTORS VP9-TEST-VECTORS-HIGH
		;;
	GStreamer-av1-V4L2SL-Gst1.0)
		echo AV1-TEST-VECTORS CHROMIUM-8bit-AV1-TEST-VECTORS CHROMIUM-10bit-AV1-TEST-VECTORS
		;;
	esac
}

VIRTME_ENV="\
MESON_BUILD_DIR=${MESON_BUILD_DIR} \
"

TEST_RESULT_FILE=$(mktemp)
TEST_SUITES_DIR="${MESON_SOURCE_DIR}/ci/fluster/visl_references"

FLUSTER_PATH=/opt/fluster
DECODER="GStreamer-${CODEC}-V4L2SL-Gst1.0"
TS=$(decoder_to_ts ${DECODER})
for ts in ${TS}; do
	TEST_COMMAND="${FLUSTER_PATH}/fluster.py -tsd ${TEST_SUITES_DIR} reference ${DECODER} ${ts}"

	SCRIPT="\
	  env $VIRTME_ENV $DIRNAME/run-virt-test.sh \
	  \\\"$TEST_COMMAND\\\" \
	  \\\"$TEST_RESULT_FILE\\\" \
	"

	echo Generating references in virtual machine ...
	virtme-run \
	  --memory=4096M \
	  --rw \
	  --pwd \
	  --kimg "$IMAGE" \
	  --script-sh "sh -c \"$SCRIPT\"" \
	  --show-boot-console --show-command \
	  --qemu-opts -cpu host,pdcm=off -smp 4
	VM_RESULT=$?
	if [ $VM_RESULT != 0 ]; then
	  echo Virtual machine exited with a failure: $VM_RESULT
	else
	  echo Virtual machine terminated.
	fi

done

exit 0
