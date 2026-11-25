#!/usr/bin/env bash
#
# Builds the power-loss lab without requiring make.
#   ./build.sh
#   CC="zig cc" ./build.sh
#   MCUBOOT=/path/to/mcuboot ./build.sh

set -euo pipefail

cd "$(dirname "$0")"

MCUBOOT="${MCUBOOT:-third_party/mcuboot}"
CC="${CC:-cc}"
MBEDTLS="$MCUBOOT/ext/mbedtls-3.6.0"
OUT="build/powerloss-lab"

if [ ! -f "$MBEDTLS/library/sha256.c" ]; then
    echo "missing $MBEDTLS — run:" >&2
    echo "  git submodule update --init third_party/mcuboot" >&2
    echo "  git -C third_party/mcuboot submodule update --init ext/mbedtls-3.6.0" >&2
    exit 1
fi

mkdir -p build

# shellcheck disable=SC2086
$CC -std=c99 -O1 -g \
    -D__BOOTSIM__ \
    -DMCUBOOT_HAVE_LOGGING \
    -DMCUBOOT_USE_FLASH_AREA_GET_SECTORS \
    -DMCUBOOT_HAVE_ASSERT_H \
    -DMCUBOOT_MAX_IMG_SECTORS=128 \
    -DMCUBOOT_BOOT_MAX_ALIGN=8 \
    -DMCUBOOT_IMAGE_NUMBER=1 \
    -DMCUBOOT_USE_MBED_TLS \
    -DCONFIG_BOOT_SWAP_USING_SCRATCH \
    -DMCUBOOT_SWAP_USING_SCRATCH \
    -I"$MCUBOOT/boot/bootutil/include" \
    -I"$MCUBOOT/sim/mcuboot-sys/csupport" \
    -I"$MCUBOOT/boot/mbed/include" \
    -I"$MBEDTLS/include" \
    -I"$MBEDTLS/library" \
    -Iport \
    "$MCUBOOT/boot/bootutil/src/image_validate.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_find_key.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_img_hash.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_img_security_cnt.c" \
    "$MCUBOOT/boot/bootutil/src/loader.c" \
    "$MCUBOOT/boot/bootutil/src/swap_misc.c" \
    "$MCUBOOT/boot/bootutil/src/swap_scratch.c" \
    "$MCUBOOT/boot/bootutil/src/swap_move.c" \
    "$MCUBOOT/boot/bootutil/src/swap_offset.c" \
    "$MCUBOOT/boot/bootutil/src/caps.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_misc.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_area.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_loader.c" \
    "$MCUBOOT/boot/bootutil/src/bootutil_public.c" \
    "$MCUBOOT/boot/bootutil/src/tlv.c" \
    "$MCUBOOT/boot/bootutil/src/fault_injection_hardening.c" \
    "$MBEDTLS/library/sha256.c" \
    "$MBEDTLS/library/platform_util.c" \
    "$MCUBOOT/sim/mcuboot-sys/csupport/run.c" \
    port/sim_port.c \
    src/main.c \
    -o "$OUT"

echo "built $OUT"
