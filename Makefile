MCUBOOT ?= third_party/mcuboot
MBEDTLS := $(MCUBOOT)/ext/mbedtls-3.6.0

CC      ?= cc
CFLAGS  ?= -std=c99 -O1 -g -Wall
CFLAGS  += -D__BOOTSIM__ \
           -DMCUBOOT_HAVE_LOGGING \
           -DMCUBOOT_USE_FLASH_AREA_GET_SECTORS \
           -DMCUBOOT_HAVE_ASSERT_H \
           -DMCUBOOT_MAX_IMG_SECTORS=128 \
           -DMCUBOOT_BOOT_MAX_ALIGN=8 \
           -DMCUBOOT_IMAGE_NUMBER=1 \
           -DMCUBOOT_USE_MBED_TLS \
           -DCONFIG_BOOT_SWAP_USING_SCRATCH \
           -DMCUBOOT_SWAP_USING_SCRATCH

INCLUDES := -I$(MCUBOOT)/boot/bootutil/include \
            -I$(MCUBOOT)/sim/mcuboot-sys/csupport \
            -I$(MCUBOOT)/boot/mbed/include \
            -I$(MBEDTLS)/include \
            -I$(MBEDTLS)/library \
            -Iport

BOOT_SRCS := $(addprefix $(MCUBOOT)/boot/bootutil/src/, \
    image_validate.c bootutil_find_key.c bootutil_img_hash.c \
    bootutil_img_security_cnt.c loader.c swap_misc.c swap_scratch.c \
    swap_move.c swap_offset.c caps.c bootutil_misc.c bootutil_area.c \
    bootutil_loader.c bootutil_public.c tlv.c fault_injection_hardening.c)

CRYPTO_SRCS := $(MBEDTLS)/library/sha256.c $(MBEDTLS)/library/platform_util.c

SIM_SRCS := $(MCUBOOT)/sim/mcuboot-sys/csupport/run.c

LOCAL_SRCS := port/sim_port.c src/main.c

SRCS := $(BOOT_SRCS) $(CRYPTO_SRCS) $(SIM_SRCS) $(LOCAL_SRCS)

BUILD := build
TARGET := $(BUILD)/powerloss-lab

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) $(BUILD)/results.csv

clean:
	rm -rf $(BUILD)
