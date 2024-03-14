#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC2153
# Usage:
# $1: The kernel image name
# $2: The list of device trees to include

set -ex

KERNEL_IMAGE_NAME=${1}
DEVICE_TREES=${2}

export KERNEL_DISABLE=" \
  ARCH_BCM \
  ARCH_NXP \
  ARCH_ACTIONS \
  ARCH_SUNXI \
  ARCH_ALPINE \
  ARCH_APPLE \
  ARCH_BCM2835 \
  ARCH_BCM_IPROC \
  ARCH_BCMBCA \
  ARCH_BRCMSTB \
  ARCH_BERLIN \
  ARCH_BITMAIN \
  ARCH_EXYNOS \
  ARCH_SPARX5 \
  ARCH_K3 \
  ARCH_LG1K \
  ARCH_HISI \
  ARCH_KEEMBAY \
  ARCH_MESON \
  ARCH_MVEBU \
  ARCH_LAYERSCAPE \
  ARCH_MXC \
  ARCH_S32 \
  ARCH_MA35 \
  ARCH_NPCM \
  ARCH_QCOM \
  ARCH_REALTEK \
  ARCH_RENESAS \
  ARCH_ROCKCHIP \
  ARCH_SEATTLE \
  ARCH_INTEL_SOCFPGA \
  ARCH_STM32 \
  ARCH_SYNQUACER \
  ARCH_TEGRA \
  ARCH_TESLA_FSD \
  ARCH_SPRD \
  ARCH_THUNDER \
  ARCH_THUNDER2 \
  ARCH_UNIPHIER \
  ARCH_VEXPRESS \
  ARCH_VISCONTI \
  ARCH_XGENE \
  ARCH_ZYNQMP \
  WIRELESS \
  RFKILL \
  WLAN \
  CAN \
  CAN_DEV \
  ETHERNET \
  BT \
  NFC \
  BTRFS_FS \
  UBIFS_FS \
  MD \
  SOUND \
  SND \
  DRM \
  INPUT \
  HID \
  CRYPTO \
  IPV6 \
  MEDIA_USB_SUPPORT \
  MEDIA_SUBDRV_AUTOSELECT \
  MEDIA_CAMERA_SUPPORT \
  MEDIA_ANALOG_TV_SUPPORT \
  MEDIA_DIGITAL_TV_SUPPORT \
  MEDIA_RADIO_SUPPORT \
  MEDIA_SDR_SUPPORT \
  MEDIA_TEST_SUPPORT \
"

export KERNEL_ENABLE=" \
  ARCH_MEDIATEK \
  PWM_MEDIATEK \
  GNSS \
  GNSS_MTK_SERIAL \
  HW_RANDOM \
  HW_RANDOM_MTK \
  MTK_DEVAPC \
  PWM_MTK_DISP \
  MTK_CMDQ \
  REGULATOR_MT6315 \
  SPMI_MTK_PMIF \
  MTK_MMSYS \
  USB_RTL8152 \
  USB_LAN78XX \
  USB_USBNET \
  MTK_SCP \
  MEDIA_PLATFORM_SUPPORT \
  MEDIA_SUPPORT \
  VIDEO_MEDIATEK_JPEG \
  VIDEO_MEDIATEK_VPU \
  VIDEO_MEDIATEK_MDP \
  VIDEO_MEDIATEK_VCODEC \
  VIDEO_MEDIATEK_MDP3 \
"

# Build a linux image for arm64 device
bash ./ci/scripts/build-linux.sh \
    "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git" \
    "${KERNEL_TAG}" \
    /lava-files/Image.gz

#mkdir -p kernel # DEBUG

ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/${KERNEL_IMAGE_NAME} \
	"${KERNEL_IMAGE_BASE}/${KERNEL_IMAGE_NAME}"

ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" /lava-files/modules.tar.zstd \
	"${KERNEL_IMAGE_BASE}/modules.tar.zstd"

mkdir -p tmp-dtbs
pushd tmp-dtbs
tar xf /lava-files/dtbs.tar.zstd

for dtb in ${DEVICE_TREES}; do
	dtb_file=$(find -name $dtb)
	if [ ! -z "${dtb_file}" ]; then
#		cp ${dtb_file} ../kernel/ # DEBUG
			ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" \
			      ${dtb_file} \
			      ${KERNEL_IMAGE_BASE}/$(basename ${dtb_file})
	fi
done

popd
rm -fr tmp-dtbs

touch done
ci-fairy s3cp --token-file "${CI_JOB_JWT_FILE}" \
	"done"
	"${KERNEL_IMAGE_BASE}/done"


#cp /lava-files/${KERNEL_IMAGE_NAME} kernel/ # DEBUG
#cp /lava-files/modules.tar.zstd kernel/ # DEBUG

