/* Memory map, MMU and caches. All regions are mapped flat. */
#include "memory.h"
#include "cache.h"
#include "mmu.h"

#define SECTION_BYTES       0x100000u
#define DDR_BASE            0x80000000u
#define DDR_BYTES           0x20000000u
#define OCMC_BASE           0x40300000u
#define PERIPHERAL_BASE     0x44000000u
#define PERIPHERAL_BYTES    0x0c000000u /* L4 and L3 peripherals, McASP data ports */
#define GPMC_REGS_BASE      0x50000000u
#define GPMC_CS0_BASE       0x01000000u
#define GPMC_CS0_BYTES      0x01000000u
#define RETAINED_REGION_BASE 0x9b000000u

static unsigned int page_table[MMU_PAGETABLE_NUM_ENTRY]
    __attribute__((aligned(MMU_PAGETABLE_ALIGN_SIZE)));

#define NORMAL_CACHED   MMU_MEMTYPE_NORMAL_NON_SHAREABLE(MMU_CACHE_WB_WA, MMU_CACHE_WB_WA)
#define NORMAL_UNCACHED MMU_MEMTYPE_NORMAL_NON_SHAREABLE(MMU_NON_CACHEABLE, MMU_NON_CACHEABLE)
#define ACCESS          MMU_AP_PRV_RW_USR_RW
#define NO_EXECUTE      (MMU_AP_PRV_RW_USR_RW | MMU_SECTION_EXEC_NEVER)

static void map(unsigned int base, unsigned int bytes, unsigned int type, unsigned int access)
{
    REGION region = {
        MMU_PGTYPE_SECTION, base, bytes / SECTION_BYTES, type,
        MMU_REGION_NON_SECURE, access, page_table,
    };
    MMUMemRegionMap(&region);
}

void memory_init(void)
{
    MMUInit(page_table);
    map(DDR_BASE, DMA_REGION_BASE - DDR_BASE, NORMAL_CACHED, ACCESS);
    map(DMA_REGION_BASE, DMA_REGION_BYTES, NORMAL_UNCACHED, NO_EXECUTE);
    map(DMA_REGION_BASE + DMA_REGION_BYTES,
        RETAINED_REGION_BASE - DMA_REGION_BASE - DMA_REGION_BYTES, NORMAL_CACHED, ACCESS);
    /* Records that must survive a reset (trace, trial handoff) go straight to DDR. */
    map(RETAINED_REGION_BASE, SECTION_BYTES, NORMAL_UNCACHED, NO_EXECUTE);
    map(RETAINED_REGION_BASE + SECTION_BYTES,
        DDR_BASE + DDR_BYTES - RETAINED_REGION_BASE - SECTION_BYTES, NORMAL_CACHED, ACCESS);
    map(OCMC_BASE, SECTION_BYTES, NORMAL_UNCACHED, NO_EXECUTE);
    map(PERIPHERAL_BASE, PERIPHERAL_BYTES, MMU_MEMTYPE_DEVICE_SHAREABLE, NO_EXECUTE);
    map(GPMC_REGS_BASE, SECTION_BYTES, MMU_MEMTYPE_DEVICE_SHAREABLE, NO_EXECUTE);
    /* The display's command/data bus: strongly ordered, so writes reach the
     * panel in program order. */
    map(GPMC_CS0_BASE, GPMC_CS0_BYTES, MMU_MEMTYPE_STRONG_ORD_SHAREABLE, NO_EXECUTE);
    MMUEnable(page_table);
    CacheEnable(CACHE_ALL);
}
