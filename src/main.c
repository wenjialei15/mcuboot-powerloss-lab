/*
 * Power-loss sweep for MCUboot's swap state machine.
 *
 * The upstream project ships a simulator, but it needs a full Rust toolchain.
 * This program links the *real* bootutil sources against a RAM flash model,
 * cuts power before every single erase/write of an upgrade, and checks
 * whether the next boot recovers.
 *
 * Cut semantics match sim/mcuboot-sys: the operation that hits zero is never
 * performed, so a cut point is "power lost just before op N".
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bootutil/bootutil.h>
#include <bootutil/bootutil_public.h>
#include <bootutil/image.h>
#include <mbedtls/sha256.h>

#include "sim_port.h"

#define IMG_PAYLOAD     8192u
#define IMG_MAX_SIZE    (IMAGE_HEADER_SIZE + IMG_PAYLOAD + 128u)

#define REBOOT_LOST     (-0x13579)

extern int invoke_boot_go(struct sim_context *ctx, struct sim_area_desc *adesc,
                          struct boot_rsp *rsp, int image_id);

static struct sim_context g_ctx;
static struct sim_area_desc *g_areas;
static FILE *g_ops_out;

static void sha256_buf(const uint8_t *data, uint32_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

/* Unsigned image: 32-byte header, payload, TLV area with the SHA256. */
static uint32_t make_image(uint8_t *buf, uint8_t major, uint8_t minor,
                           uint16_t revision, uint32_t build_num, uint8_t seed)
{
    struct image_header *hdr = (struct image_header *)buf;
    struct image_tlv_info *info;
    struct image_tlv *tlv;
    uint32_t off;
    uint32_t i;

    memset(buf, 0, IMG_MAX_SIZE);

    hdr->ih_magic = IMAGE_MAGIC;
    hdr->ih_load_addr = 0x08000000u;
    hdr->ih_hdr_size = IMAGE_HEADER_SIZE;
    hdr->ih_protect_tlv_size = 0;
    hdr->ih_img_size = IMG_PAYLOAD;
    hdr->ih_flags = 0;
    hdr->ih_ver.iv_major = major;
    hdr->ih_ver.iv_minor = minor;
    hdr->ih_ver.iv_revision = revision;
    hdr->ih_ver.iv_build_num = build_num;

    for (i = 0; i < IMG_PAYLOAD; i++) {
        buf[IMAGE_HEADER_SIZE + i] = (uint8_t)(seed + i * 7u);
    }

    off = IMAGE_HEADER_SIZE + IMG_PAYLOAD;

    info = (struct image_tlv_info *)(buf + off);
    info->it_magic = IMAGE_TLV_INFO_MAGIC;
    info->it_tlv_tot = sizeof(*info) + sizeof(*tlv) + IMAGE_HASH_LEN;

    tlv = (struct image_tlv *)(buf + off + sizeof(*info));
    tlv->it_type = IMAGE_TLV_SHA256;
    tlv->it_len = IMAGE_HASH_LEN;

    sha256_buf(buf, off, buf + off + sizeof(*info) + sizeof(*tlv));

    return off + info->it_tlv_tot;
}

static void prepare_context(void)
{
    sim_set_flash_areas(g_areas);
    sim_set_context(&g_ctx);
}

static int run_boot(struct boot_rsp *rsp)
{
    g_ctx.flash_counter = INT_MAX;
    g_ctx.jumped = 0;
    prepare_context();
    return invoke_boot_go(&g_ctx, g_areas, rsp, -1);
}

/* Slot 0: 1.0.0. Slot 1: 1.1.0, marked pending. */
static void prime_device(int permanent, uint32_t sector)
{
    uint8_t img[IMG_MAX_SIZE];
    uint32_t len;

    sim_port_init(sector);
    g_areas = sim_port_areas();

    g_ctx.flash_counter = INT_MAX;
    g_ctx.jumped = 0;
    prepare_context();

    len = make_image(img, 1, 0, 0, 0, 0x11);
    sim_flash_write(0, SIM_SLOT0_OFF, img, len);

    len = make_image(img, 1, 1, 0, 1, 0x5a);
    sim_flash_write(0, SIM_SLOT1_OFF, img, len);

    if (boot_set_pending(permanent) != 0) {
        fprintf(stderr, "boot_set_pending failed\n");
        exit(2);
    }

    sim_port_snapshot();
}

static void image_version_at(uint32_t off, int *major, int *minor)
{
    const struct image_header *hdr =
        (const struct image_header *)(sim_port_device() + off);

    if (hdr->ih_magic != IMAGE_MAGIC) {
        *major = -1;
        *minor = -1;
        return;
    }
    *major = hdr->ih_ver.iv_major;
    *minor = hdr->ih_ver.iv_minor;
}

struct result {
    int reboot_rc;
    int major;
    int minor;
    int second_boot_clean;
};

static int sweep(const char *title, int want_major, int want_minor)
{
    struct boot_rsp rsp;
    struct sim_op *base_ops;
    int total_ops;
    int failures = 0;
    int i;

    sim_port_restore();
    if (run_boot(&rsp) != 0) {
        printf("%s: baseline boot FAILED\n\n", title);
        return 1;
    }

    total_ops = sim_port_op_count();
    base_ops = malloc(sizeof(struct sim_op) * (size_t)total_ops);
    if (base_ops == NULL) {
        return 1;
    }
    memcpy(base_ops, sim_port_ops(), sizeof(struct sim_op) * (size_t)total_ops);

    if (g_ops_out != NULL) {
        fprintf(g_ops_out, "seq,kind,area,off,len\n");
        for (i = 0; i < total_ops; i++) {
            uint32_t off = base_ops[i].off;
            const char *area = off < SIM_BOOT_SIZE ? "boot" :
                               off < SIM_SLOT1_OFF ? "slot0" :
                               off < SIM_SCRATCH_OFF ? "slot1" : "scratch";
            fprintf(g_ops_out, "%d,%s,%s,0x%05x,%u\n", i + 1,
                    base_ops[i].kind == SIM_OP_ERASE ? "erase" : "write",
                    area, off, base_ops[i].len);
        }
        fclose(g_ops_out);
        g_ops_out = NULL;
    }

    printf("== %s ==\n", title);
    printf("clean run: %d flash ops, boots %d.%d\n\n", total_ops,
           rsp.br_hdr->ih_ver.iv_major, rsp.br_hdr->ih_ver.iv_minor);

    for (i = 1; i <= total_ops; i++) {
        struct result r;
        const struct sim_op *op = &base_ops[i - 1];
        int rc;

        sim_port_restore();
        g_ctx.flash_counter = i;
        g_ctx.jumped = 0;
        prepare_context();
        rc = invoke_boot_go(&g_ctx, g_areas, &rsp, -1);

        if (rc == REBOOT_LOST) {
            r.reboot_rc = run_boot(&rsp);
            image_version_at(SIM_SLOT0_OFF, &r.major, &r.minor);

            sim_port_clear_ops();
            run_boot(&rsp);
            r.second_boot_clean = (sim_port_op_count() == 0);
        } else {
            r.reboot_rc = rc;
            image_version_at(SIM_SLOT0_OFF, &r.major, &r.minor);
            r.second_boot_clean = 1;
        }

        if (r.reboot_rc != 0 || r.major != want_major ||
            r.minor != want_minor || !r.second_boot_clean) {
            failures++;
            printf("  cut %2d before %-5s @0x%05x -> reboot=%d slot0=%d.%d%s\n",
                   i, op->kind == SIM_OP_ERASE ? "erase" : "write", op->off,
                   r.reboot_rc, r.major, r.minor,
                   r.second_boot_clean ? "" : " (writes again!)");
        }
        sim_port_restore();
    }

    printf("%s: %d cut points, %d failures\n\n", title, total_ops, failures);
    free(base_ops);
    return failures;
}

/* A corrupted secondary image must never be swapped in. */
static int corrupt_secondary_test(uint32_t sector)
{
    struct boot_rsp rsp;
    int major;
    int minor;

    prime_device(1, sector);
    sim_port_device()[SIM_SLOT1_OFF + IMAGE_HEADER_SIZE + 100] ^= 0xff;

    if (run_boot(&rsp) != 0) {
        printf("corrupt secondary: boot FAILED\n\n");
        return 1;
    }

    image_version_at(SIM_SLOT0_OFF, &major, &minor);
    printf("== corrupted secondary image ==\n");
    printf("booted %d.%d (expected 1.0), rejection %s\n\n", major, minor,
           (major == 1 && minor == 0) ? "works" : "BROKEN");

    return (major == 1 && minor == 0) ? 0 : 1;
}

int main(void)
{
    static const uint32_t sectors[] = { 4096u, 1024u };
    int failures = 0;
    unsigned i;
    char title[96];

    setvbuf(stdout, NULL, _IONBF, 0);

    g_ops_out = fopen("build/ops.csv", "w");

    for (i = 0; i < sizeof(sectors) / sizeof(sectors[0]); i++) {
        uint32_t sector = sectors[i];

        prime_device(1, sector);
        snprintf(title, sizeof(title),
                 "permanent upgrade 1.0.0 -> 1.1.0 (sector %u B)", sector);
        failures += sweep(title, 1, 1);

        prime_device(0, sector);
        {
            struct boot_rsp rsp;
            if (run_boot(&rsp) != 0) {
                printf("test upgrade baseline failed\n");
                return 1;
            }
        }
        sim_port_snapshot();
        snprintf(title, sizeof(title),
                 "test revert 1.1.0 -> 1.0.0 (sector %u B)", sector);
        failures += sweep(title, 1, 0);

        failures += corrupt_secondary_test(sector);
    }

    printf("TOTAL: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
