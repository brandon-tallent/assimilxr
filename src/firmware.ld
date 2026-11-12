/* The Tallent Edition accepts trial images of the Assimil8or app's file
 * size with the identity record at a fixed address. */
OUTPUT_ARCH(arm)
ENTRY(_start)

IMAGE_BASE = 0x80000000;
IDENTITY_ADDRESS = 0x8006e4f8;
IMAGE_END = 0x8009bac8;
DMA_BASE = 0x9a000000;
DMA_END = 0x9a100000;
HEAP_LIMIT = 0x99000000;
/* Reload (app/reload.c): the image as it started, the next one being
 * staged, and the trampoline that copies it into place */
reload_pristine = 0x9c000000;
reload_stage = 0x9c100000;
reload_trampoline = 0x9c200000;
image_start = IMAGE_BASE;
image_end = IMAGE_END;
ASSERT(IMAGE_END - IMAGE_BASE <= reload_stage - reload_pristine, "the image outgrows its reload copies")

SECTIONS
{
    . = IMAGE_BASE;
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        . = ALIGN(4);
        KEEP(*(.init)) KEEP(*(.fini))
        __preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array)) __fini_array_end = .;
        *(.rodata*)
        *(.data*)
        . = ALIGN(8);
        text_end = .;
    }
    ASSERT(. <= IDENTITY_ADDRESS, "code reaches the identity record")
    . = IDENTITY_ADDRESS;
    .identity : { KEEP(*(.identity)) KEEP(*(.identity.marker)) }
    . = IMAGE_END;
    .bss (NOLOAD) : ALIGN(64) {
        bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        bss_end = .;
    }
    .stacks (NOLOAD) : ALIGN(16) {
        . += 0x1000; fiq_stack_top = .;
        . += 0x4000; irq_stack_top = .;
        . += 0x1000; abort_stack_top = .;
        . += 0x10000; system_stack_top = .;
        . += 0x10000; stack_top = .;
        heap_start = .;
    }
    heap_end = HEAP_LIMIT;
    .dma DMA_BASE (NOLOAD) : { *(.dma) }
    ASSERT(. <= DMA_END, "DMA buffers exceed their uncached section")
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.ARM.extab*) }
}
