/* The FatFs disk layer on the SD cards: physical drive n is SD drive n. */
#include "ff.h"
#include "diskio.h"
#include "sd.h"

DSTATUS disk_status(BYTE drive)
{
    return drive < 2 && sd_ready(drive) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive >= 2) return STA_NOINIT;
    return sd_ready(drive) || sd_init(drive) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    return sd_read(drive, buffer, (uint32_t)sector, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    return sd_write(drive, buffer, (uint32_t)sector, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    if (!sd_ready(drive)) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC: return RES_OK;     /* writes finish before they return */
    case GET_SECTOR_SIZE: *(WORD *)data = SD_BLOCK_BYTES; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)data = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
