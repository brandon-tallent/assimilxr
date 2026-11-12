/* SD cards on MMCHS0 (the system card) and MMCHS2 (the front slot),
 * through the StarterWare MMC/SD protocol layer with polled transfers. */
#ifndef STORAGE_SD_H
#define STORAGE_SD_H

#include <stdint.h>

#define SD_BLOCK_BYTES      512u

/* Powers the controller and identifies the card. Returns 1 if a card is
 * ready. */
int sd_init(unsigned drive);
int sd_ready(unsigned drive);
uint32_t sd_blocks(unsigned drive);
int sd_read(unsigned drive, void *buffer, uint32_t block, uint32_t count);
int sd_write(unsigned drive, const void *buffer, uint32_t block, uint32_t count);

#endif
