#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

/* One uncached DDR section holds every DMA buffer, so no cache maintenance
 * is needed around transfers. Put buffers there with DMA_BUFFER. */
#define DMA_REGION_BASE     0x9a000000u
#define DMA_REGION_BYTES    0x00100000u
#define DMA_BUFFER          __attribute__((section(".dma"), aligned(32)))

void memory_init(void);

#endif
