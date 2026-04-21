#pragma once

/**
 * Annulla il tentativo di connessione WiFi avviato al boot.
 * No-op se il tentativo è già terminato (successo, fallimento o annullato).
 */
void wifi_boot_abort();

/**
 * Ritorna true se il tentativo di connessione WiFi al boot è ancora in corso.
 */
bool wifi_boot_is_active();
