/* Pictures of a voice's settings, drawn into a box on the screen. */
#ifndef UI_GRAPHS_H
#define UI_GRAPHS_H

/* waveform is the LXR oscillator waveform (OscWaveforms), pwm 0-127 */
void graph_wave(int waveform, int pwm, int x, int y, int w, int h);
/* The amp envelope for the LXR's 0-127 attack, decay and slope */
void graph_envelope(int attack, int decay, int slope, int x, int y, int w, int h);
/* The pitch envelope */
void graph_decay(int decay, int slope, int x, int y, int w, int h);
/* type is the filter type (filterTypeEnum), cutoff and resonance 0-127 */
void graph_filter(int type, int cutoff, int resonance, int x, int y, int w, int h);
/* Times in ms: the amp envelope's attack (decay_part 0) or decay, the pitch envelope */
unsigned graph_envelope_ms(int attack, int decay, int slope, int decay_part);
unsigned graph_decay_ms(int decay, int slope);
/* The filter's cutoff in Hz for its 0-127 value */
float graph_cutoff_hz(int cutoff);

#endif
