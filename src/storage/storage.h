/* The two SD cards and their FAT file systems. Drive 0: is the system
 * card inside the module (app, cal.bin); drive 1: is the card in the front
 * slot. Main loop only: transfers are polled. */
#ifndef STORAGE_STORAGE_H
#define STORAGE_STORAGE_H

#include <stdint.h>

#define STORAGE_DRIVES      2u
#define STORAGE_SYSTEM      0u
#define STORAGE_CARD        1u

/* Mounts both drives. Returns a bit per mounted drive. */
unsigned storage_init(void);

/* A bit per drive mounted by storage_init */
unsigned storage_mounted(void);

/* Reads up to size bytes of a file into buffer. Returns the bytes read,
 * or -1 if the file cannot be opened. */
int storage_read(const char *path, void *buffer, uint32_t size);

/* A file written in pieces, one at a time: begin creates `<path>.part`
 * (and any folders on its path), each piece goes at its offset, end closes
 * it, renames it to path over any old file and gives the CRC-32 (zlib) of
 * what was written in order. A write that stops early leaves path as it
 * was. Return 1 on success. */
int storage_write_begin(const char *path);
int storage_write_at(uint32_t offset, const void *data, uint32_t size);
int storage_write_end(uint32_t *crc, uint32_t *size);

/* CRC-32 as zlib's: start from STORAGE_CRC32_INITIAL, invert the result */
#define STORAGE_CRC32_INITIAL   0xffffffffu
uint32_t storage_crc32(uint32_t crc, const uint8_t *data, uint32_t size);

#endif
