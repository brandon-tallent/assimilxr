/* The Assimil8or's wiring: which SoC blocks, pins and chips do what. */
#ifndef BOARD_H
#define BOARD_H

#include "soc_AM335x.h"

/* Audio: McASP0 drives the eight-channel DAC (four I2S lines, two slots
 * each, 48 kHz, clock master from AHCLKX). McASP1 is the stereo codec
 * link (mix outputs and Sample inputs, 192 kHz, clocks from the codec). */
#define BOARD_OUTPUT_MCASP          SOC_MCASP_0_CTRL_REGS
#define BOARD_OUTPUT_MCASP_FIFO     SOC_MCASP_0_FIFO_REGS
#define BOARD_OUTPUT_MCASP_DATA     SOC_MCASP_0_DATA_REGS
#define BOARD_OUTPUT_SERIALIZERS    4
#define BOARD_CODEC_MCASP           SOC_MCASP_1_CTRL_REGS
#define BOARD_CODEC_MCASP_FIFO      SOC_MCASP_1_FIFO_REGS
#define BOARD_CODEC_MCASP_DATA      SOC_MCASP_1_DATA_REGS
#define BOARD_CODEC_TX_SERIALIZER   0
#define BOARD_CODEC_RX_SERIALIZER   1

/* Codec and DAC control, I2C0 at 100 kHz. */
#define BOARD_I2C                   SOC_I2C_0_REGS
#define BOARD_AUDIO_RESET_GPIO      SOC_GPIO_2_REGS
#define BOARD_AUDIO_RESET_PIN_A     22
#define BOARD_AUDIO_RESET_PIN_B     23
/* High unmutes the mix outputs. */
#define BOARD_MIX_ENABLE_GPIO       SOC_GPIO_2_REGS
#define BOARD_MIX_ENABLE_PIN        24
/* A strap that selects the audio parts fitted: low for Cirrus, high for AKM. */
#define BOARD_AUDIO_VARIANT_GPIO    SOC_GPIO_2_REGS
#define BOARD_AUDIO_VARIANT_PIN     10

/* SSD1351 128x128 OLED on GPMC chip select 0, 8 bits wide; address line
 * A18 selects command (low) or data (high). */
#define BOARD_DISPLAY_BUS           0x01000000u
#define BOARD_DISPLAY_DATA_OFFSET   (1u << 18)
#define BOARD_DISPLAY_GPIO          SOC_GPIO_2_REGS
#define BOARD_DISPLAY_RESET_PIN     2
#define BOARD_DISPLAY_POWER_PIN     5 /* low switches the panel supply on */

/* CV inputs: two ADCs, on McSPI0 (channels 1-4) and McSPI1 (5-8). */
#define BOARD_CV_LINK_0             SOC_SPI_0_REGS
#define BOARD_CV_LINK_1             SOC_SPI_1_REGS

/* SD cards: MMCHS0 for the system card inside, MMCHS2 (on GPMC pads) for
 * the front slot. */
#define BOARD_SD_SYSTEM             SOC_MMCHS_0_REGS
#define BOARD_SD_CARD               SOC_MMCHS_2_REGS

/* MIDI on UART0. */
#define BOARD_MIDI_UART             SOC_UART_0_REGS

void board_pins_init(void);

#endif
