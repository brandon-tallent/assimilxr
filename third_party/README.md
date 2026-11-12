# Third-party code

All of it is under licenses compatible with GPL-3. Each file keeps its
original copyright notice.

| Directory | Source | License |
| --- | --- | --- |
| `starterware/` | Texas Instruments AM335x StarterWare 02.00.01.01 (from the unmodified mirror github.com/kryochronic/AM335X_StarterWare_02_00_01_01): headers, the McASP, EDMA, I2C, GPIO, UART, watchdog, timer, GPMC, McSPI, SD and ADC drivers, and the interrupt, cache, MMU and CP15 code | BSD-3-Clause, in each file |
| `starterware/mmcsdlib/` | The StarterWare MMC/SD protocol layer (`mmcsd_proto.c`, `hs_mmcsdlib.c`) from the same mirror | BSD-3-Clause, in each file |
| `fatfs/` | ChaN FatFs R0.15 (elm-chan.org/fsw/ff/arc/ff15.zip), `ff.c`, `ff.h`, `diskio.h`, `ffunicode.c` | FatFs licence (BSD-style, 1 clause), `fatfs/LICENSE.txt` |
| `cmsis/Core_A/` | Arm CMSIS 5 Core(A) compiler and CP15 headers | Apache-2.0, `cmsis/LICENSE.txt` |
| `fonts/spleen/` | Spleen 5x8, 6x12, 8x16 and 12x24 by Frederic Cambus (commit `5116f8e`), converted with `tools/bdf2c.py` | BSD-2-Clause, `fonts/spleen/LICENSE` |

## Changes

- `starterware/system_config/exceptionhandler.S`: the IRQ entry also saves
  D16-D31, which NEON code uses as scratch, and keeps the System-mode stack
  8-byte aligned. The changed lines are marked `AssimiLXR`.
- `starterware/mmcsdlib/`: no UART messages, and no cache maintenance
  around transfers, since the CPU moves the data (`AssimiLXR`).
- `fatfs/ffconf.h`: two volumes, long names in static memory, exFAT, code
  page 437, no clock; otherwise as released.
