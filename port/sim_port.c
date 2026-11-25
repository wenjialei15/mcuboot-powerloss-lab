/*
 * RAM-backed flash model for the MCUboot bootutil sources.
 *
 * Everything upstream needs from a platform lives here:
 *   - the flash device itself (256 KiB, erased to 0xff)
 *   - a sector map per flash area
 *   - the sim_* hooks that run.c calls instead of touching real hardware
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_port.h"

static uint8_t g_flash[SIM_FLASH_SIZE];
static uint8_t g_pristine[SIM_FLASH_SIZE];

static struct flash_area g_slot_areas[4][SIM_MAX_SECTORS];
static struct sim_area_desc g_desc;

static struct sim_op g_ops[SIM_MAX_OPS];
static uint32_t g_sector_size = SIM_SECTOR_SIZE;
static int g_op_count;

static struct sim_area_desc *g_live_desc;
static struct sim_context *g_live_ctx;

/* ------------------------------------------------------------------------- */
/* Area table                                                                */
/* ------------------------------------------------------------------------- */

static void add_area(int id, uint32_t off, uint32_t size)
{
    struct sim_area *slot = &g_desc.slots[g_desc.num_slots];
    uint32_t count = size / g_sector_size;
    uint32_t i;

    if (count > SIM_MAX_SECTORS) {
        fprintf(stderr, "area %d has too many sectors\n", id);
        exit(1);
    }

    for (i = 0; i < count; i++) {
        g_slot_areas[id][i].fa_id = (uint8_t)id;
        g_slot_areas[id][i].fa_device_id = 0;
        g_slot_areas[id][i].pad16 = 0;
        g_slot_areas[id][i].fa_off = off + i * g_sector_size;
        g_slot_areas[id][i].fa_size = g_sector_size;
    }

    slot->whole.fa_id = (uint8_t)id;
    slot->whole.fa_device_id = 0;
    slot->whole.pad16 = 0;
    slot->whole.fa_off = off;
    slot->whole.fa_size = size;
    slot->areas = g_slot_areas[id];
    slot->num_areas = count;
    slot->id = (uint8_t)id;

    g_desc.num_slots++;
}

void sim_port_init(uint32_t sector_size)
{
    g_sector_size = sector_size;
    memset(g_flash, 0xff, sizeof(g_flash));
    memset(g_pristine, 0xff, sizeof(g_pristine));
    memset(&g_desc, 0, sizeof(g_desc));

    /* Order matters: bootutil assumes primary, secondary, scratch. */
    add_area(0, SIM_BOOT_OFF, SIM_BOOT_SIZE);
    add_area(1, SIM_SLOT0_OFF, SIM_SLOT_SIZE);
    add_area(2, SIM_SLOT1_OFF, SIM_SLOT_SIZE);
    add_area(3, SIM_SCRATCH_OFF, SIM_SCRATCH_SIZE);

    g_op_count = 0;
}

void sim_port_snapshot(void)
{
    memcpy(g_pristine, g_flash, sizeof(g_flash));
}

void sim_port_restore(void)
{
    memcpy(g_flash, g_pristine, sizeof(g_flash));
    g_op_count = 0;
}

uint8_t *sim_port_device(void)
{
    return g_flash;
}

struct sim_area_desc *sim_port_areas(void)
{
    return &g_desc;
}

const struct sim_op *sim_port_ops(void)
{
    return g_ops;
}

int sim_port_op_count(void)
{
    return g_op_count;
}

void sim_port_clear_ops(void)
{
    g_op_count = 0;
}

/* ------------------------------------------------------------------------- */
/* Hooks required by sim/mcuboot-sys/csupport/run.c                          */
/* ------------------------------------------------------------------------- */

struct sim_area_desc *sim_get_flash_areas(void)
{
    return g_live_desc;
}

void sim_set_flash_areas(struct sim_area_desc *areas)
{
    g_live_desc = areas;
}

void sim_reset_flash_areas(void)
{
    g_live_desc = NULL;
}

struct sim_context *sim_get_context(void)
{
    return g_live_ctx;
}

void sim_set_context(struct sim_context *ctx)
{
    g_live_ctx = ctx;
}

void sim_reset_context(void)
{
    g_live_ctx = NULL;
}

static void log_op(uint8_t kind, uint32_t off, uint32_t len)
{
    if (g_op_count < SIM_MAX_OPS) {
        g_ops[g_op_count].kind = kind;
        g_ops[g_op_count].off = off;
        g_ops[g_op_count].len = len;
    }
    g_op_count++;
}

int sim_flash_read(uint8_t flash_id, uint32_t offset, uint8_t *dest,
                   uint32_t size)
{
    (void)flash_id;

    if (offset > SIM_FLASH_SIZE || size > SIM_FLASH_SIZE - offset) {
        return -1;
    }

    memcpy(dest, &g_flash[offset], size);
    return 0;
}

int sim_flash_write(uint8_t flash_id, uint32_t offset, const uint8_t *src,
                    uint32_t size)
{
    (void)flash_id;

    if (offset > SIM_FLASH_SIZE || size > SIM_FLASH_SIZE - offset) {
        return -1;
    }

    log_op(SIM_OP_WRITE, offset, size);
    memcpy(&g_flash[offset], src, size);
    return 0;
}

int sim_flash_erase(uint8_t flash_id, uint32_t offset, uint32_t size)
{
    (void)flash_id;

    if (offset > SIM_FLASH_SIZE || size > SIM_FLASH_SIZE - offset) {
        return -1;
    }

    log_op(SIM_OP_ERASE, offset, size);
    memset(&g_flash[offset], 0xff, size);
    return 0;
}

uint32_t sim_flash_align(uint8_t flash_id)
{
    (void)flash_id;
    return 8;
}

uint8_t sim_flash_erased_val(uint8_t flash_id)
{
    (void)flash_id;
    return 0xff;
}

/* run.c logs through this hook; keep it to warnings and errors. */
int sim_log_enabled(int level)
{
    return level <= 2;
}
