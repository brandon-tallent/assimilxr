/* The LXR menu tables used by the UI: pages, parameter names, value types
 * and value formatting, from the LXR firmware's menu.c (GPL-3.0-only). */
#ifndef LXR_MENU_H
#define LXR_MENU_H

#include "menu.h"

extern const Name valueNames[NUM_NAMES];
extern const enum Datatypes parameter_dtypes[NUM_PARAMS];
extern const Page menuPages[NUM_PAGES][NUM_SUB_PAGES];

/* Text tables from MenuText.h */
const char *lxr_short_name(uint8_t name);
const char *lxr_long_name(uint8_t name);
const char *lxr_category_name(uint8_t name);

void numtostrpu(char *buf, uint8_t num, char pad);
void numtostrps(char *buf, int8_t num);
void numtostru(char *buf, uint8_t num);
void numtostrs(char *buf, int8_t num);
void upr_three(char *buf);
void setNoteName(uint8_t num, char *buf);
/* Menu numbers the UI needs (MENU_WAVEFORM and MENU_TRANS in MenuText.h,
 * which holds the menu data itself) */
#define LXR_MENU_WAVEFORM   2
#define LXR_MENU_TRANSIENT  10
uint8_t getMaxEntriesForMenu(uint8_t menuId);
void getMenuItemNameForValue(const uint8_t menuId, const uint8_t curParmVal, char *buf);

#endif
