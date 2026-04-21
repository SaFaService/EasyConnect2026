#ifndef API_MANAGER_H
#define API_MANAGER_H

#include <Arduino.h>

/**
 * ITA: Modulo di invio dati verso API remote (factory/customer).
 * ENG: Data dispatch module for remote APIs (factory/customer).
 *
 * ITA: Espone una singola funzione pubblica che raccoglie i dati runtime
 *      (DeltaP, risorse, stato slave) e li invia agli endpoint configurati.
 * ENG: Exposes one public function that gathers runtime data
 *      (DeltaP, resources, slave status) and sends it to configured endpoints.
 */

/**
 * ITA: Costruisce e invia il payload JSON agli endpoint configurati.
 * ENG: Builds and sends the JSON payload to configured endpoints.
 */
void sendDataToRemoteServer();

#endif
