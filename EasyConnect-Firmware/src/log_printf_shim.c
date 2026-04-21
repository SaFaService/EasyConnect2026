/*
 * Linker shim for log_printf.
 * Shim linker per log_printf.
 *
 * When pioarduino rebuilds ESP-IDF components from source (triggered by
 * sdkconfig.defaults changes), a duplicate-symbol conflict can arise between
 * the pre-compiled Arduino framework and the freshly-built esp_log component.
 * The GNU linker --wrap mechanism redirects every call to log_printf through
 * this wrapper, which delegates to log_printfv and breaks the conflict.
 *
 * Build flag required (already set in platformio.ini):
 *   -Wl,--wrap=log_printf
 *
 * ITA:
 * Quando alcuni componenti ESP-IDF vengono ricompilati localmente, puo'
 * comparire un conflitto di simboli su log_printf. Questa wrapper function
 * redirige le chiamate verso log_printfv e rimuove il conflitto in linking.
 */

#include <stdarg.h>

extern int log_printfv(const char *format, va_list arg);

int __wrap_log_printf(const char *format, ...)
{
    /* ITA/ENG: forward varargs safely to log_printfv. */
    va_list args;
    va_start(args, format);
    int ret = log_printfv(format, args);
    va_end(args);
    return ret;
}
