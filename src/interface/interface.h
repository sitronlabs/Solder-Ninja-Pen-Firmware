#ifndef INTERFACE_H
#define INTERFACE_H

/**
 *
 */
enum page {
    PAGE_NONE,
    PAGE_SPLASH,
    PAGE_LOCKED,
    PAGE_MONITOR,
    PAGE_MENU_LIST,
    PAGE_MENU_ITEM,
};

// It is the interface that is locked
// Or is it the app ?
// Interface unlocks, can the station unlock ?
// Magnet absent->present : locks
// Magnet present->absent : unlocks
// Both buttons press : locks/unlocks
// Com can unlock

/**
 * Nope, interface should be autonomous
 * It lives its own life
 */
// int interface_show(const enum page page);

/**
 *
 */
int interface_setup(void);

/**
 *
 * Interface should poll data as the communication link would.
 */
int interface_task(void);  // Calls buttons, and display tasks

#endif
