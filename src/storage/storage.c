/* Mounting and whole-file reads. */
#include "storage.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

static FATFS volumes[STORAGE_DRIVES];
static const char *const roots[STORAGE_DRIVES] = { "0:", "1:" };

static unsigned mounted;

unsigned storage_init(void)
{
    mounted = 0;
    for (unsigned d = 0; d < STORAGE_DRIVES; ++d)
        if (f_mount(&volumes[d], roots[d], 1) == FR_OK) mounted |= 1u << d;
    return mounted;
}

unsigned storage_mounted(void) { return mounted; }

int storage_read(const char *path, void *buffer, uint32_t size)
{
    FIL file;
    UINT got = 0;
    if (f_open(&file, path, FA_READ) != FR_OK) return -1;
    FRESULT result = f_read(&file, buffer, size, &got);
    f_close(&file);
    return result == FR_OK ? (int)got : -1;
}

#define CRC32_POLYNOMIAL    0xedb88320u     /* zlib, reflected */
#define PATH_BYTES          96u

/* A file arrives as `<path>.part` and replaces `path` only once closed */
#define PART_SUFFIX         ".part"
#define OLD_SUFFIX          ".old"
static FIL writing;
static int writing_open;
static char writing_path[PATH_BYTES], part_path[PATH_BYTES + sizeof PART_SUFFIX],
            old_path[PATH_BYTES + sizeof OLD_SUFFIX];
static uint32_t written, crc;

uint32_t storage_crc32(uint32_t c, const uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i) {
        c ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) c = c >> 1 ^ (CRC32_POLYNOMIAL & -(c & 1u));
    }
    return c;
}

/* Creates each folder on the path; ones already there are fine */
static void make_folders(const char *path)
{
    char folder[PATH_BYTES];
    size_t length = strlen(path);
    if (length >= sizeof folder) return;
    memcpy(folder, path, length + 1);
    for (char *slash = strchr(folder + 3, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        f_mkdir(folder);
        *slash = '/';
    }
}

int storage_write_begin(const char *path)
{
    if (writing_open) { f_close(&writing); writing_open = 0; }
    if (strlen(path) >= PATH_BYTES) return 0;
    make_folders(path);
    snprintf(writing_path, sizeof writing_path, "%s", path);
    snprintf(part_path, sizeof part_path, "%s" PART_SUFFIX, path);
    snprintf(old_path, sizeof old_path, "%s" OLD_SUFFIX, path);
    if (f_open(&writing, part_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    writing_open = 1;
    written = 0;
    crc = STORAGE_CRC32_INITIAL;
    return 1;
}

/* A piece at the end extends the file; one repeated at its offset is
 * written again and leaves the CRC as it was */
int storage_write_at(uint32_t offset, const void *data, uint32_t size)
{
    UINT done = 0;
    if (!writing_open || offset > written) return 0;
    if (offset < written && offset + size != written) return 0;
    if (f_lseek(&writing, offset) != FR_OK || f_write(&writing, data, size, &done) != FR_OK || done != size)
        return 0;
    if (offset == written) { crc = storage_crc32(crc, data, size); written += size; }
    return 1;
}

int storage_write_end(uint32_t *crc_out, uint32_t *size)
{
    if (!writing_open) return 0;
    writing_open = 0;
    *crc_out = ~crc;
    *size = written;
    if (f_close(&writing) != FR_OK) { f_unlink(part_path); return 0; }
    /* The old file is set aside until the new one has its name */
    f_unlink(old_path);
    FRESULT kept = f_rename(writing_path, old_path);
    if (kept != FR_OK && kept != FR_NO_FILE) return 0;
    if (f_rename(part_path, writing_path) != FR_OK) {
        if (kept == FR_OK) f_rename(old_path, writing_path);
        return 0;
    }
    f_unlink(old_path);
    return 1;
}
