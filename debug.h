#include <stdarg.h>
#include "types.h"

#ifndef DEBUG_H
#define DEBUG_H

#ifdef DEBUG

/**
 * Prints a formatted debug message to the console. The message can be
 * formatted as if you were calling printf.
 *
 * This will only work if the program is compiled with the debug flag.
 * To do that, use the -DDEBUG flag when compiling:
 *
 *   gcc <input_file> -DDEBUG
 *
 * When compiling for production, do not compile with the debug flag,
 * or else the console output will be unnecessarily cluttered.
 */
static inline void debug_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("\x1B[36m");
    vprintf(format, args);
    printf("\x1B[0m");
    va_end(args);
}

#define DEBUG_PRINTF(...) debug_printf(__VA_ARGS__)

static inline void debug_print_start_message() {
    debug_printf("EECS Assignment 3 Debug Mode\n");
    debug_printf("============================\n");
    debug_printf("\n");
    debug_printf("Messages in blue (this color) are debug ");
    debug_printf("messages and white text is the actual output of ");
    debug_printf("the program.");
    debug_printf("\n\n");
}

#define DEBUG_PRINT_START_MESSAGE() debug_print_start_message()

static inline void debug_message(const char *message) {
    debug_printf("%s", message);
    debug_printf("\n");
}

#define DEBUG_MESSAGE(message) debug_message(message);

static inline void debug_print_alarm_request_without_newline(alarm_request_t *alarm_request) {
    debug_printf(
        "{id: %d, type: %s, time: %d, message: %s, "
        "creation_time: %ld, next: %p}",
        alarm_request->alarm_id,
        request_type_string( alarm_request),
        alarm_request->time,
        alarm_request->message,
        alarm_request->creation_time,
        alarm_request->next
    );
}

static inline void debug_print_alarm_request(alarm_request_t *alarm_request) {
    debug_print_alarm_request_without_newline(alarm_request);
    debug_printf("\n");
}

#define DEBUG_PRINT_ALARM_REQUEST(alarm_request) debug_print_alarm_request(alarm_request)

static inline void debug_print_alarm_list(alarm_request_t *alarm_list_header) {
    alarm_request_t *alarm_request = alarm_list_header;
    debug_printf("[");
    while (alarm_request != NULL) {
        debug_print_alarm_request_without_newline(alarm_request);
        alarm_request = alarm_request->next;
        if (alarm_request != NULL) {
            debug_printf(", ");
        }
    }
    debug_printf("]\n");
}
#define DEBUG_PRINT_ALARM_LIST(alarm_list_header) debug_print_alarm_list(alarm_list_header)

#else // DEBUG

#define DEBUG_PRINTF(...)

#define DEBUG_MESSAGE(message)

#define DEBUG_PRINT_START_MESSAGE()

#define DEBUG_PRINT_ALARM_REQUEST(alarm_request)

#define DEBUG_PRINT_ALARM_LIST(alarm_list_header)

#endif // DEBUG
#endif // DEBUG_H

