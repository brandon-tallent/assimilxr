/* Kits on the front card, in KITS_FOLDER: LXR-02 kits (.snd) and this
 * firmware's own (.kit), which hold all eight channels. */
#ifndef APP_KITS_H
#define APP_KITS_H

#include <stddef.h>
#include <stdint.h>

#define KITS_FOLDER         "1:/AssimiLXR"
#define KITS_OSC_WAVES      "osc"           /* user wave folders in KITS_FOLDER, not kits */
#define KITS_TRANSIENT_WAVES "transients"
#define KITS_ALL_CHANNELS   (-1)

/* A folder or kit file in a listing */
#define KITS_NAME_BYTES     48u
struct kits_entry { char name[KITS_NAME_BYTES]; uint8_t folder; };

/* Lists the folders and kit files in a folder, folders first, each by
 * name. Returns how many, at most max. */
unsigned kits_list(const char *folder, struct kits_entry *entries, unsigned max);

/* Creates KITS_FOLDER if it is missing. Returns 0 without a card. */
int kits_folder(void);

/* Loads a .snd or .kit into one channel or all of them, as the kit or, with
 * into_morph, as the morph kit. A .snd leaves each channel's voice; a whole
 * .kit also sets the morph amount and its CV route. Returns 1 when loaded. */
int kits_load(const char *path, int channel, int into_morph);

/* Reads the kit files in a folder (as kits_list gave them, folders left
 * out) into memory, up to KITS_CACHE, replacing those read before.
 * kits_load_cached then loads one into every channel without the card,
 * the morph kits and CV routes included. */
#define KITS_CACHE          64u
#define KITS_PATH_BYTES     160u
unsigned kits_cache(const char *folder, const struct kits_entry *kits, unsigned count);
int kits_load_cached(unsigned index);

/* A kit's name: 8 characters, as an LXR-02 kit's */
#define KITS_KIT_NAME_BYTES 8u

/* Saves the channels as NAME.kit (replacing one of that name), or as the
 * next free KITnnn.kit for a blank name. With blend, the kit is the morph
 * blend as it sounds, without a morph kit. The file name goes to saved.
 * Returns 1 when saved. */
int kits_save(const char *kit_name, int blend, char *saved, size_t size);

/* Saves a channel's sound as an LXR-02 kit (.snd), named as the LXR-02
 * does: NN-XXXXX.snd with the next free number. */
int kits_save_snd(unsigned channel, const char *kit_name, int blend, char *saved, size_t size);

#endif
