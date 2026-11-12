/* Promote, restore and the boot guard. */
#include "install.h"
#include "remote.h"
#include "storage.h"
#include "ff.h"
#include "trace.h"
#include <stdio.h>
#include <string.h>

#define APP_PATH            "0:/app"
#define APP_OLD_PATH        "0:/appold"
#define APP_SWAP_PATH       "0:/appswap"
#define APP_TRIAL_PATH      "0:/apptrial"
#define APP_TRIAL_RUN_PATH  "0:/apptrial.run"
#define APP_NEW_PATH        "0:/appnew"
#define APP_STOCK_PATH      "0:/appstock"
#define CARD_APP_PATH       "1:/app"
#define COPY_CHUNK_BYTES    (64u * 1024u)
/* The identity record in an image file: an 8-byte header, then the
 * program loaded at IMAGE_LOAD_ADDRESS */
#define IMAGE_HEADER_BYTES  8u
#define IMAGE_LOAD_ADDRESS  0x80000000u
#define IDENTITY_FILE_OFFSET (IMAGE_HEADER_BYTES + ((uint32_t)&identity - IMAGE_LOAD_ADDRESS))

#define RESET_WDT1          (1u << 4)   /* PRM_RSTST: watchdog 1 reset */

/* Reads an image file's identity record; 0 if it has none */
static int image_identity(const char *path, struct identity *out)
{
    FIL file;
    UINT got = 0;
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    int ok = f_lseek(&file, IDENTITY_FILE_OFFSET) == FR_OK &&
             f_read(&file, out, sizeof *out, &got) == FR_OK && got == sizeof *out;
    f_close(&file);
    return ok && !memcmp(out->magic, identity.magic, sizeof out->magic);
}

int install_promote(void)
{
    struct identity trial;
    if (!image_identity(APP_TRIAL_RUN_PATH, &trial) ||
        memcmp(trial.build_id, identity.build_id, sizeof trial.build_id)) return 0;
    f_unlink(APP_OLD_PATH);
    if (f_rename(APP_PATH, APP_OLD_PATH) != FR_OK) return 0;
    if (f_rename(APP_TRIAL_RUN_PATH, APP_PATH) != FR_OK) {
        f_rename(APP_OLD_PATH, APP_PATH);
        return 0;
    }
    return 1;
}

/* An application image: its 8-byte header gives the file's length, header
 * included, and the load address. Installing checks both. */
static int image_header_ok(const char *path)
{
    FIL file;
    UINT got = 0;
    uint32_t header[2];
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    int ok = f_read(&file, header, sizeof header, &got) == FR_OK && got == sizeof header &&
             header[1] == IMAGE_LOAD_ADDRESS && header[0] > IMAGE_HEADER_BYTES &&
             header[0] <= f_size(&file);
    f_close(&file);
    return ok;
}

/* Whether an image is this firmware: its marker after the identity record */
static int image_is_ours(const char *path)
{
    FIL file;
    UINT got = 0;
    char marker[REMOTE_MARKER_BYTES];
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    int ok = f_lseek(&file, IDENTITY_FILE_OFFSET + sizeof identity) == FR_OK &&
             f_read(&file, marker, sizeof marker, &got) == FR_OK && got == sizeof marker;
    f_close(&file);
    return ok && !memcmp(marker, identity_marker, sizeof marker);
}

static int copy_file(const char *from, const char *to);
int install_previous(char *name, size_t size)
{
    struct identity previous;
    if (!image_header_ok(APP_OLD_PATH)) return 0;
    if (image_identity(APP_OLD_PATH, &previous))
        snprintf(name, size, "build %02x%02x%02x%02x", previous.build_id[0], previous.build_id[1],
                 previous.build_id[2], previous.build_id[3]);
    else snprintf(name, size, "stock firmware");
    return 1;
}

int install_card_available(void) { return image_header_ok(CARD_APP_PATH); }

static uint8_t copy_buffer[COPY_CHUNK_BYTES];

/* The CRC of a whole file, or 0 for an unreadable one */
static int file_crc(const char *path, uint32_t *crc, uint32_t *bytes)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    uint32_t c = STORAGE_CRC32_INITIAL, total = 0;
    UINT got;
    FRESULT result;
    while ((result = f_read(&file, copy_buffer, sizeof copy_buffer, &got)) == FR_OK && got) {
        c = storage_crc32(c, copy_buffer, got);
        total += got;
    }
    f_close(&file);
    *crc = ~c;
    *bytes = total;
    return result == FR_OK;
}

static int copy_file(const char *from, const char *to)
{
    FIL in, out;
    if (f_open(&in, from, FA_READ) != FR_OK) return 0;
    if (f_open(&out, to, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) { f_close(&in); return 0; }
    UINT got, written;
    FRESULT result;
    int ok = 1;
    while ((result = f_read(&in, copy_buffer, sizeof copy_buffer, &got)) == FR_OK && got)
        if (f_write(&out, copy_buffer, got, &written) != FR_OK || written != got) { ok = 0; break; }
    ok = ok && result == FR_OK;
    f_close(&in);
    ok = f_close(&out) == FR_OK && ok;
    uint32_t source_crc, source_bytes, copy_crc, copy_bytes;
    ok = ok && file_crc(from, &source_crc, &source_bytes) && file_crc(to, &copy_crc, &copy_bytes) &&
         source_crc == copy_crc && source_bytes == copy_bytes;
    if (!ok) f_unlink(to);
    return ok;
}

/* Makes `from` the installed app as the stock Load Software does: a copy
 * as `appnew`, then `app` to `appold` and `appnew` to `app` */
static int install_from(const char *from)
{
    if (!image_header_ok(from) || !copy_file(from, APP_NEW_PATH)) return 0;
    f_unlink(APP_OLD_PATH);
    if (f_rename(APP_PATH, APP_OLD_PATH) != FR_OK) return 0;
    if (f_rename(APP_NEW_PATH, APP_PATH) != FR_OK) { f_rename(APP_OLD_PATH, APP_PATH); return 0; }
    return 1;
}

/* Writes a program image with its header as `<drive>:/appnew`, reads it
 * back, then renames it over `app` */
static int write_image(char drive, const uint8_t *image, uint32_t bytes)
{
    char app[] = "0:/app", fresh[] = "0:/appnew", swap[] = "0:/appswap";
    app[0] = fresh[0] = swap[0] = drive;
    FIL file;
    UINT written;
    uint32_t header[2] = { IMAGE_HEADER_BYTES + bytes, IMAGE_LOAD_ADDRESS };
    if (f_open(&file, fresh, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    int ok = f_write(&file, header, sizeof header, &written) == FR_OK && written == sizeof header &&
             f_write(&file, image, bytes, &written) == FR_OK && written == bytes;
    ok = f_close(&file) == FR_OK && ok;
    uint32_t crc, size;
    uint32_t want = ~storage_crc32(storage_crc32(STORAGE_CRC32_INITIAL, (const uint8_t *)header, sizeof header), image, bytes);
    ok = ok && file_crc(fresh, &crc, &size) && crc == want && size == sizeof header + bytes;
    if (!ok) { f_unlink(fresh); return 0; }
    f_unlink(swap);
    FRESULT kept = f_rename(app, swap);
    if (kept != FR_OK && kept != FR_NO_FILE) return 0;
    if (f_rename(fresh, app) != FR_OK) { if (kept == FR_OK) f_rename(swap, app); return 0; }
    f_unlink(swap);
    return 1;
}

int install_image(const uint8_t *image, uint32_t bytes) { return write_image('0', image, bytes); }

int install_card_image(const uint8_t *image, uint32_t bytes) { return write_image('1', image, bytes); }

int install_load_card(void) { return install_from(CARD_APP_PATH); }

int install_stock_available(void) { return image_header_ok(APP_STOCK_PATH); }

int install_stock(void) { return install_from(APP_STOCK_PATH); }

void install_keep_stock(void)
{
    if (image_header_ok(APP_STOCK_PATH) || !image_header_ok(APP_OLD_PATH) || image_is_ours(APP_OLD_PATH)) return;
    copy_file(APP_OLD_PATH, APP_STOCK_PATH);
}

int install_restore(void)
{
    if (!image_header_ok(APP_PATH) || !image_header_ok(APP_OLD_PATH)) return 0;
    f_unlink(APP_TRIAL_PATH);
    f_unlink(APP_SWAP_PATH);
    if (f_rename(APP_PATH, APP_SWAP_PATH) != FR_OK) return 0;
    if (f_rename(APP_OLD_PATH, APP_PATH) != FR_OK) {
        f_rename(APP_SWAP_PATH, APP_PATH);
        return 0;
    }
    return f_rename(APP_SWAP_PATH, APP_OLD_PATH) == FR_OK;
}

int install_boot(uint32_t boot_mode, uint32_t reset_causes)
{
    if (boot_mode != BOOT_STABLE) return 1;   /* a trial returns to stock by itself */
    /* The watchdog restarted an installed build that stopped: put back the
     * firmware it replaced and restart at once */
    if (reset_causes & RESET_WDT1) {
        trace_stage(STAGE_RESTORE);
        if (install_stock() || install_restore()) remote_reset_now();
        return 0;
    }
    install_keep_stock();
    return 1;
}
