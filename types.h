#ifndef TYPES_H
#define TYPES_H
#include <stdbool.h>

/**
 * The six possible types of commands that a user can enter.
 */
typedef enum request_type {
    Start_Alarm,
    Change_Alarm,
    Cancel_Alarm
} request_type;

/**
 * Data structure representing an alarm request.
 */
typedef struct alarm_request_t {
    int alarm_id;
    request_type type;
    int time;
    char message[128];
    time_t creation_time;
    struct alarm_request_t *next;
} alarm_request_t;

/**
 * Retuns the name of an enum value for request_type enum (instead of
 * the value of the enum, which is an integer).
 */
static inline const char *request_type_string(alarm_request_t *alarm_request) {
    /*
     * Array of the names of the names of the enum values as strings.
     *
     * They are declared in the same order as the enum values above, which means
     * they can be indexed by the enum value to get the name that corresponds to
     * the value.
     *
     * This is declared as static so that it is retained between function calls
     * (more efficient than re-initializing every time this function is called).
     */
    static const char *enum_names[] = {
        "Start_Alarm",
        "Change_Alarm",
        "Cancel_Alarm"
    };

    /*
     * Return the string representation of the enum name corresponding to the
     * given value of the enum in the alarm request.
     */
    return enum_names[alarm_request->type];
}

/**
 * Data type representing a periodic display thread. This is how the alarm
 * thread will create periodic display threads, and is the type of the parameter
 * that will be given to periodic display threads upon creation.
 */
typedef struct periodic_display_thread_t {
    int thread_id;
    pthread_t thread;
    int time;
    struct periodic_display_thread_t *next;
} periodic_display_thread_t;

#endif

