/* Display names: the parameters on the voice and FX pages by LXR name,
 * and the LXR's three-letter waveform and filter values in full. The LXR's
 * own are cut to fit a 16-column LCD. */
#include "names.h"
#include "lxr_menu.h"
#include <string.h>

static const struct { uint8_t id; const char *name; } names[] = {
    { TEXT_COARSE, "Coarse" }, { TEXT_FINE, "Fine" }, { TEXT_WAVEFORM, "Waveform" },
    { TEXT_PWM, "Pulse width" }, { TEXT_ATTACK, "Attack" }, { TEXT_DECAY, "Decay" },
    { TEXT_EG_SLOPE, "Slope" }, { TEXT_REPEAT, "Repeats" }, { TEXT_DECAY_CLOSED, "Closed decay" },
    { TEXT_DECAY_OPEN, "Open decay" }, { TEXT_PITCH_DECAY, "Pitch decay" },
    { TEXT_MOD_AMOUNT, "Pitch amount" }, { TEXT_VEL_DEST, "Velocity to" },
    { TEXT_VEL_AMT, "Velocity amt" }, { TEXT_VEL_MOD_VOL, "Velocity level" },
    { TEXT_FM_AMOUNT, "FM amount" }, { TEXT_FM_FREQ, "FM frequency" }, { TEXT_MODE, "Mode" },
    { TEXT_MOD_OSC1_FREQ, "Mod 1 freq" }, { TEXT_MOD_OSC2_FREQ, "Mod 2 freq" },
    { TEXT_MOD_OSC1_GAIN, "Mod 1 level" }, { TEXT_MOD_OSC2_GAIN, "Mod 2 level" },
    { TEXT_NOISE, "Noise freq" }, { TEXT_MIX, "Noise mix" },
    { TEXT_TRANSIENT_WAVE, "Waveform" }, { TEXT_TRANSIENT_VOLUME, "Level" },
    { TEXT_TRANSIENT_FREQ, "Frequency" }, { TEXT_FILTER_F, "Cutoff" },
    { TEXT_FILTER_RESO, "Resonance" }, { TEXT_FILTER_TYPE, "Type" }, { TEXT_FILTER_DRIVE, "Drive" },
    { TEXT_FREQ_LFO, "Rate" }, { TEXT_SYNC_LFO, "Sync" }, { TEXT_MOD_LFO, "Amount" },
    { TEXT_WAVE_LFO, "Waveform" }, { TEXT_RETRIGGER_LFO, "Retrigger" }, { TEXT_OFFSET_LFO, "Phase" },
    { TEXT_TARGET_VOICE_LFO, "Target voice" }, { TEXT_TARGET_LFO, "Target" },
    { TEXT_VOLUME, "Level" }, { TEXT_PAN, "Pan" }, { TEXT_SAMPLE_RATE, "Sample rate" },
    { TEXT_DRIVE, "Drive" }, { TEXT_AUDIO_OUT, "Output" },
    { TEXT_FX_TYPE, "Type" }, { TEXT_FX_ROUTING, "Output" }, { TEXT_FX_DRY_WET, "Dry/wet" },
    { TEXT_FX_DIST_DRIVE, "Drive" }, { TEXT_FX_COLOR, "Color" }, { TEXT_FX_TONE, "Tone" },
    { TEXT_FX_VOLUME, "Level" }, { TEXT_FX_WAVE, "Waveform" }, { TEXT_FX_FREQ, "Frequency" },
    { TEXT_FX_RATIO, "Ratio" }, { TEXT_FX_COMP_ATTACK, "Attack" }, { TEXT_FX_COMP_DECAY, "Decay" },
    { TEXT_FX_COMP_THRESHOLD, "Threshold" }, { TEXT_FX_GAIN, "Gain" }, { TEXT_FX_TIME, "Time" },
    { TEXT_FX_RANGE, "Range" }, { TEXT_FX_FEEDBACK, "Feedback" }, { TEXT_FX_PREDELAY, "Predelay" },
    { TEXT_FX_REFLECTION, "Reflections" },
};

const char *ui_text_name(uint8_t text)
{
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (names[i].id == text) return names[i].name;
    return lxr_long_name(text);
}

static const struct { const char *lxr, *name; } value_names[] = {
    { "Sin", "Sine" }, { "Tri", "Triangle" }, { "Saw", "Saw" }, { "Rec", "Square" },
    { "Noi", "Noise" }, { "LP", "Lowpass" }, { "HP", "Highpass" }, { "BP", "Bandpass" },
    { "UBP", "Unity BP" }, { "Nch", "Notch" }, { "Pek", "Peak" }, { "LP2", "Lowpass 2" },
    { "off", "Off" }, { "on ", "On" }, { "mix", "Mix" }, { "fm ", "FM" },
    { "sin", "Sine" }, { "tri", "Triangle" }, { "sup", "Saw up" }, { "sdn", "Saw down" },
    { "sqr", "Square" }, { "rnd", "Random" }, { "xup", "Exp up" }, { "xdn", "Exp down" },
    { "St1", "Stereo 1" }, { "St2", "Stereo 2" }, { "L1", "Left 1" }, { "R1", "Right 1" },
    { "L2", "Left 2" }, { "R2", "Right 2" }, { "FX", "FX bus" },
    { "drv", "Drive" }, { "rng", "Ring mod" }, { "cmp", "Compressor" }, { "del", "Delay" },
    { "rev", "Reverb" }, { "Plt", "Plate" }, { "Del", "Delay" }, { "Pp", "Ping-pong" },
    { "tub", "Tube" }, { "fld", "Fold" }, { "clp", "Clip" },
    { "Snp", "Snap" }, { "Ofs", "Offset" }, { "Clk", "Click" }, { "Ck2", "Click 2" },
    { "Tik", "Tick" }, { "Kik", "Kick" }, { "Drp", "Drop" }, { "Clp", "Clap" },
    { "Kk2", "Kick 2" }, { "Snr", "Snare" }, { "Sp2", "Snap 2" },
};

const char *ui_value_name(const char *lxr)
{
    for (unsigned i = 0; i < sizeof value_names / sizeof value_names[0]; ++i)
        if (!strcmp(lxr, value_names[i].lxr)) return value_names[i].name;
    return 0;
}
