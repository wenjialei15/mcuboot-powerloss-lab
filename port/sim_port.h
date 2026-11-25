/*
 * PC-side port of the MCUboot simulator flash backend.
 *
 * The upstream simulator implements these symbols in Rust (sim/mcuboot-sys).
 * This port replaces that dependency with a plain C RAM/flash model so the
 * real bootutil sources can run on a workstation with only a C compiler.
 */

#ifndef SIM_PORT_H
#define SIM_PORT_H

#include <setjmp.h>
#include <stdint.h>

#include <flash_map_backend/flash_map_backend.h>

/* 256 KiB device, uniform 4 KiB sectors: 64 sectors in total. */
#define SIM_FLASH_SIZE      (256u * 1024u)
#define SIM_SECTOR_SIZE     4096u

#define SIM_BOOT_OFF        0x00000u
#define SIM_BOOT_SIZE       0x08000u

#define SIM_SLOT0_OFF       0x08000u
#define SIM_SLOT1_OFF       0x18000u
#define SIM_SLOT_SIZE       0x10000u

#define SIM_SCRATCH_OFF     0x28000u
#define SIM_SCRATCH_SIZE    0x01000u

#define SIM_MAX_SECTORS     64
#define SIM_MAX_OPS         4096

struct sim_area {
    struct flash_area whole;
    struct flash_area *areas;
    uint32_t num_areas;
    uint8_t id;
};

struct sim_area_desc {
    struct sim_area slots[16];
    uint32_t num_slots;
};

/* Mirrors the layout used by sim/mcuboot-sys/csupport/run.c. */
struct sim_context {
    int flash_counter;
    int jumped;
    uint8_t c_asserts;
    uint8_t c_catch_asserts;
    jmp_buf boot_jmpbuf;
};

enum sim_op_kind {
    SIM_OP_ERASE = 1,
    SIM_OP_WRITE = 2,
};

struct sim_op {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t off;
    uint32_t len;
};

/* Builds the area table and erases the device. */
void sim_port_init(uint32_t sector_size);

/* Hooks consumed by the upstream run.c harness. */
struct sim_area_desc *sim_get_flash_areas(void);
void sim_set_flash_areas(struct sim_area_desc *areas);
void sim_reset_flash_areas(void);
struct sim_context *sim_get_context(void);
void sim_set_context(struct sim_context *ctx);
void sim_reset_context(void);

int sim_flash_read(uint8_t flash_id, uint32_t offset, uint8_t *dest,
                   uint32_t size);
int sim_flash_write(uint8_t flash_id, uint32_t offset, const uint8_t *src,
                    uint32_t size);
int sim_flash_erase(uint8_t flash_id, uint32_t offset, uint32_t size);
uint32_t sim_flash_align(uint8_t flash_id);
uint8_t sim_flash_erased_val(uint8_t flash_id);

/* Copies the current device contents into the pristine snapshot. */
void sim_port_snapshot(void);

/* Restores the pristine snapshot and clears the op log. */
void sim_port_restore(void);

uint8_t *sim_port_device(void);

struct sim_area_desc *sim_port_areas(void);

const struct sim_op *sim_port_ops(void);
int sim_port_op_count(void);

/* Zeroes the operation log without touching device contents. */
void sim_port_clear_ops(void);

#endif /* SIM_PORT_H */
