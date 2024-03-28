#include <pthread.h>
#include "errors.h"
#include "types.h"
#include "debug.h"
#include "Command_Parser.h"

#define USER_INPUT_BUFFER_SIZE 256

/**
 * Header of the alarm list. The is the data structure that is shared between
 * the main thread and the alarm thread.
 */
alarm_request_t alarm_list_header = {0};

/**
 * Mutex for the alarm list. Any thread reading or modifying the alarm list must
 * have this mutex locked.
 */
pthread_mutex_t alarm_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Condition variable for the alarm list. This allows the alarm thread to wait
 * for updates to the alarm list.
 */
pthread_cond_t alarm_list_cond = PTHREAD_COND_INITIALIZER;

void *alarm_thread(void *arg) {
    return NULL;
}

/**
 * Inserts the alarm request in its specified position in the alarm list (sorted
 * by their time values).
 */
void insert_to_alarm_list(alarm_request_t *alarm_request) {
    alarm_request_t *current = &alarm_list_header;
    alarm_request_t *next = alarm_list_header.next;

    // Find where to insert it by comparing alarm_id. The
    // list should always be sorted by alarm_id.
    while (next != NULL) {
        if (alarm_request->time < next->time) {
            // Insert before next
            current->next = alarm_request;
            alarm_request->next = next;
            return;
        } else {
            current = next;
            next = next->next;
        }
    }

    // Insert at the end of the list
    current->next = alarm_request;
    alarm_request->next = NULL;
}

/**
 * Handles a comamnd.
 */
void handle_request(alarm_request_t *alarm_request) {
    insert_to_alarm_list(alarm_request);
}

void handle_request_thread_safe(alarm_request_t *alarm_request) {
    /*
     * Lock mutex
     */
    pthread_mutex_lock(&alarm_list_mutex);

    /*
     * Handle command
     */
    insert_to_alarm_list(alarm_request);

    /*
     * Print success message
     */
    printf(
        "Main Thread has Inserted Alarm_Request_Type %s Request(%d) at "
        "%ld: Time = %d Message = %s into Alarm List\n",
        request_type_string(alarm_request),
        alarm_request->alarm_id,
        time(NULL),
        alarm_request->time,
        alarm_request->message
    );

    /*
     * Unlock mutex
     */
    pthread_mutex_unlock(&alarm_list_mutex);
}

/**
 * Main thread.
 *
 * The main thread is responsible for creating one alarm thread and one consumer
 * thread.
 */
int main() {
    char input[USER_INPUT_BUFFER_SIZE]; // Buffer to store user input.

    alarm_request_t *alarm_request;     // Most recent alarm request (data
                                        // structure representing the user's
                                        // request).

    DEBUG_PRINT_START_MESSAGE();

    while (1) {
        printf("Alarm > ");

        /*
         * Get a request from user input. If NULL, then the user did not enter a
         * command.
         */
        if (fgets(input, USER_INPUT_BUFFER_SIZE, stdin) == NULL) {
            printf("Bad command\n");
            continue;
        }
        DEBUG_PRINTF("%d\n",sizeof(input));

        // Replace newline with null terminating character
        input[strcspn(input, "\n")] = 0;

        /*
         * Parse user's request.
         */
        alarm_request = parse_request(input);

        /*
         * If alarm_request is NULL, then the request was invalid.
         */
        if (alarm_request == NULL) {
            printf("Bad command\n");
            continue;
        } else {
            /*
             * Handle the alarm request.
             */
            handle_request_thread_safe(alarm_request);
        }

        DEBUG_PRINT_ALARM_LIST(&alarm_list_header);
    }
}

