/* Remote editing over the MIDI port's SysEx protocol: reading and setting
 * each channel's sound, mix and CV routes, the shared FX bus and morph,
 * the kits on the card and pictures of the waves. docs/development.md has
 * the wire format. */
#ifndef APP_EDITOR_H
#define APP_EDITOR_H

#include <stdint.h>

/* Commands, under the remote protocol's numbering: numbers the Tallent
 * Edition does not use, so a client of both never mistakes one for the
 * other. EDITOR_KITS lists, loads and saves kits by its first byte. */
enum { EDITOR_SUMMARY = 27, EDITOR_READ = 28, EDITOR_SET = 29, EDITOR_KITS = 30, EDITOR_WAVE = 31 };
enum { EDITOR_KITS_LIST, EDITOR_KITS_LOAD, EDITOR_KITS_SAVE };

/* Handles one editor command: the reply's data in out (at most out_max
 * bytes) and its count. Returns the status (the remote protocol's OK,
 * BAD_LENGTH, BAD_RANGE) or -1 for a command that is not the editor's. */
int editor_command(uint32_t command, const uint8_t *p, uint32_t bytes, uint8_t *out, uint32_t out_max,
                   uint32_t *count);

#endif
