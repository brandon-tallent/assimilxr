/* This firmware as the module's installed one, after the patched stock
 * firmware's file scheme on the system card: `app` boots at power-up,
 * `appold` is the one before, a trial is staged by the stock firmware and
 * renamed `apptrial.run` when it boots. */
#ifndef APP_INSTALL_H
#define APP_INSTALL_H

#include <stddef.h>
#include <stdint.h>

/* Makes the running trial `app` (the old `app` becomes `appold`). Returns
 * 1 when done; on a failed rename it tries to put `app` back. */
int install_promote(void);

/* Swaps `app` and `appold` (each must be an application image: the stock
 * firmware, this one or another) and drops a staged trial. Returns 1 when
 * done; the next start boots the other. */
int install_restore(void);

/* The previous firmware (`appold`), as "stock firmware" or a build; 0 if
 * there is none */
int install_previous(char *name, size_t size);

/* Installs `1:/app` from the front card as the stock Load Software does
 * (copy to `appnew`, then `app` to `appold` and `appnew` to `app`), after
 * checking its header and reading the copy back. Returns 1 when done. */
int install_card_available(void);
int install_load_card(void);

/* Makes a program image `app` (the 8-byte header is added): written as
 * `appnew` and read back, then swapped in by renames, so a failure leaves
 * `app` as it was. Returns 1 when done. */
int install_image(const uint8_t *image, uint32_t bytes);
/* The same as the front card's `app`, so installing from it gives this build */
int install_card_image(const uint8_t *image, uint32_t bytes);

/* The stock firmware is kept as `appstock`: the first start as `app` copies
 * a stock `appold` there, and nothing replaces it. install_stock installs
 * it as Load Software would, so the module goes back to stock without the
 * file. */
void install_keep_stock(void);
int install_stock_available(void);
int install_stock(void);

/* Boot guard for an installed build, called at start once storage is
 * mounted: after a watchdog reset it puts back `appstock`, or else
 * `appold`, and restarts. Returns 0 when that failed. */
int install_boot(uint32_t boot_mode, uint32_t reset_causes);

#endif
