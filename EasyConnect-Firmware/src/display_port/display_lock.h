#ifndef DISPLAY_LOCK_H_
#define DISPLAY_LOCK_H_

/*
 * Blocco di stabilizzazione del driver display.
 *
 * Questi valori sono stati "congelati" dopo una fase di tuning sul pannello
 * reale. Non sono semplici costanti di comodo: definiscono la combinazione di
 * clock e anti-tearing considerata affidabile per questa board.
 *
 * Idea pratica:
 * - se si cambia qualcosa qui, non si sta facendo refactoring innocuo;
 * - si sta modificando il comportamento elettrico/temporale del display;
 * - quindi la modifica va sempre validata su hardware.
 */

#define DISPLAY_DRIVER_LOCK_ENABLED            (1)
#define DISPLAY_LOCK_PCLK_HZ                   (30 * 1000 * 1000)
#define DISPLAY_LOCK_AVOID_TEAR_ENABLE         (1)
#define DISPLAY_LOCK_AVOID_TEAR_MODE           (3)

#endif  // DISPLAY_LOCK_H_

