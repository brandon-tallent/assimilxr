#ifndef BOARD_CODEC_H
#define BOARD_CODEC_H

enum { CODEC_VARIANT_CIRRUS, CODEC_VARIANT_AKM };

/* Resets and configures the codec and DAC. Returns an I2C error code. */
int codec_init(void);
int codec_variant(void);
/* The mix outputs stay muted until enabled, so start-up makes no pop. */
void codec_mix_enable(int on);

#endif
