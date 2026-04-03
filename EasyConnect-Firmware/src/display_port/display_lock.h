#ifndef DISPLAY_LOCK_H_
#define DISPLAY_LOCK_H_

// Display driver freeze lock
// Snapshot date: 2026-04-03
// Keep these values stable unless an intentional unlock is requested.

#define DISPLAY_DRIVER_LOCK_ENABLED            (1)
#define DISPLAY_LOCK_PCLK_HZ                   (30 * 1000 * 1000)
#define DISPLAY_LOCK_AVOID_TEAR_ENABLE         (1)
#define DISPLAY_LOCK_AVOID_TEAR_MODE           (3)

#endif  // DISPLAY_LOCK_H_

