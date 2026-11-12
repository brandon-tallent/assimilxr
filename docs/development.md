# AssimiLXR developer guide

The MIDI remote commands, the fault trace and how firmware is switched.
The [README](../README.md) covers building and installing; the
[manual](https://brandon-tallent.github.io/assimilxr/manual.html) covers
using it.

## MIDI remote commands

The MIDI port answers the
[Tallent Edition's remote protocol](https://github.com/brandon-tallent/assimil8or-extended-firmware/blob/main/PROTOCOL.md):
Identify, Diagnostics, Peek, Poke, Reboot, Panel (remote button presses)
and Screenshot. Three more commands write a file to either card:

| Command | Payload | Reply |
| --- | --- | --- |
| 45 Begin | the path, `0:/` or `1:/` first | none |
| 46 Data | u32 offset, then the bytes for that offset | none |
| 47 End | none | u32 CRC-32 and u32 length of the file written |

Folders on the path are created. The sender compares the CRC-32 with its
own.

Four more replace the running build without a restart
(`src/app/reload.c`):

| Command | Payload | Reply |
| --- | --- | --- |
| 48 Reload begin | u32 image length, u32 CRC-32 of the image | none; refused if the length is not this build's |
| 49 Reload data | u32 stream offset, then stream bytes; sequence 0 sends it without a reply | none |
| 50 Reload end | none | u32 CRC-32 and u32 length of the staged image; refused if it does not match Begin |
| 51 Reload run | `SQA8` | 1 if the front card's `app` was written too, else 0; an installed build first makes the image `app` (refused if that fails); then the new build starts |

At reset, `start.S` copies the image as loaded to `0x9c000000`. The
stream rebuilds the new image at `0x9c100000` from that copy: a varint
`v`, then `v / 2` literal bytes when `v` is even, or, when it is odd,
`v / 2` bytes copied from the reference position moved by a zigzag varint,
which then advances past them. Run sets the trial handoff with the new
build ID, stops audio, the CV links, DMA and interrupts, and jumps through
a trampoline at `0x9c200000` that turns the MMU off, copies the image to
`0x80000000` and enters it, as the Tallent Edition's chain-load does. A
reset still returns to the installed firmware.

Five more commands serve a remote editor (`src/app/editor.c`):

| Command | Payload | Reply |
| --- | --- | --- |
| 27 Summary | none | a u16 Fletcher-16 sum for each channel (its sound, CV routes and morph kit), then the kit's and the user waves' |
| 28 Read | record, u16 offset, count (at most 120) | that part of the record |
| 29 Set | channel, kind, ID, the value as four nibbles low first | none |
| 30 Kits | 0 List: u16 first entry, folder; 1 Load: channel (255 all), morph, path; 2 Save: kind (0 `.kit`, 1 `.snd`), channel, blend, name | List: u16 count, entries returned, then each entry's folder flag and name; Save: the file name |
| 31 Wave | kind (0 user oscillator wave, 1 transient), number, columns, first column, count (at most 60) | u32 length in samples, then the lowest and highest sample of each column |

Folder and kit paths are relative to `AssimiLXR/`. A Set with sequence
0 has no reply, so it can be sent as automation. The records:

| Record | Bytes | Contents |
| --- | --- | --- |
| 0-7 | 256 | A channel's sound parameters (0-246, as a kit stores them), then its voice (248: 0-6, 7 none), mute, FX send, play mode (Gated), pitch input and accent input (0 none, 1-3 its own A-C), and whether it has a morph kit |
| 8-15 | 494 | A channel's CV routes: a source and a signed amount a parameter |
| 16-23 | 247 | A channel's morph kit, zeros without one |
| 24 | 256 | Morph as set and with CV, its route, tempo in tenths of BPM, MIDI clock running, the selected channel, the user wave counts, the Sample input levels, the MIDI setup at 12-28, the kit name at 32 and folder at 80 |
| 25 | 1280 | The user waves' names: 16 oscillator waves, then 16 transients |

Set kinds:

| Kind | ID | Value |
| --- | --- | --- |
| 0 Parameter | a sound parameter; an FX bus parameter sets every channel | its value |
| 1 Role | a row of `editor_roles` (`src/app/editor_roles.h`): the parameter for the voice the channel plays | its value |
| 2 Channel | 0 voice, 1 mute, 2 FX send, 3 play mode, 4 pitch input, 5 accent input | as in the record |
| 3 Kit | 0 morph, 1 morph route source, 2 morph route amount + 100, 3 and 4 the Sample input levels, 5 the selected channel | its value |
| 4, 5 Route source, amount | a sound parameter | the source, or the amount + 100 |

## Fault trace

`0x9b0000c0` holds a record that survives a watchdog reset: the start-up
stage reached, main-loop and audio-block counts, and the address and
status of any CPU fault. After a failed start, read it with Peek from the
firmware that runs next. The layout is `struct trace` in
`src/platform/trace.h`.

## Switching firmware

- On its first start as `app`, AssimiLXR copies a stock `0:/appold` to
  `0:/appstock` and never writes that file again.
- Utilities > Firmware > Stock firmware installs `appstock` the way a
  stock update does and restarts.
- Utilities > Firmware > Load from card installs any `1:/app`, checking its
  header and reading the copy back first.
- An installed build reset by the watchdog puts the stock copy back (or
  else swaps `app` and `appold`) and restarts at once. If that fails, every
  page shows "Watchdog: restore failed".
- On a module running the Tallent Edition, its install command stages a
  build as a one-shot watchdog trial; a reset returns to the Tallent
  Edition.
- Over MIDI, Promote (23) and Restore (24) make a trial build the
  installed `app`, or swap `app` and `appold`.
