#include <pthread.h>
#include "errors.h"
#include "types.h"
#include "debug.h"
#include "Command_Parser.h"

#define USER_INPUT_BUFFER_SIZE 256
#define CIRCULAR_BUFFER_SIZE 4

/*******************************************************************************
 *                             CONSUMER THREAD                                 *
 ******************************************************************************/

/**
 * Consumer thread.
 */
void *consumer_thread_routine(void *arg) {
    DEBUG_MESSAGE("Consumer thread running.");

    return NULL;
}

/*******************************************************************************
 *           DATA SHARED BETWEEN CONSUMER THREAD AND ALARM THREAD              *
 ******************************************************************************/

/**
 * Circular buffer used to store alarms.
 */
alarm_request_t* circularBuffer[CIRCULAR_BUFFER_SIZE] = {NULL};

/**
 * Size of the circular buffer used to store alarms. 
 */
int circularBufferSize = 0;

/**
 * Integer used to keep track of where to write 
 */
int writeIndex = 0;

/*******************************************************************************
 *             DATA SHARED BETWEEN MAIN THREAD AND ALARM THREAD                *
 ******************************************************************************/

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

/*******************************************************************************
 *                               ALARM THREAD                                  *
 ******************************************************************************/

/**
 * Alarm thread.
 */
void *alarm_thread_routine(void *arg) {
    DEBUG_MESSAGE("Alarm thread running.");

    return NULL;
}

/*******************************************************************************
 *                      HELPER FUNCTIONS FOR ALARM THREAD                      *
 ******************************************************************************/

/**
 * Adds an alarm to the circular buffer.
 */
void write_to_circular_buffer(alarm_request_t *alarm_request) {
    int current = -1;
    if (circularBufferSize >= CIRCULAR_BUFFER_SIZE) {
        printf("\nCircular buffer size is full.\n");
    }
    else {
        circularBuffer[writeIndex] = alarm_request;
        circularBufferSize = circularBufferSize + 1;
        writeIndex = writeIndex + 1;
        if (writeIndex == CIRCULAR_BUFFER_SIZE) {
            writeIndex = 0;
        }
    }
}

/*******************************************************************************
 *                     HELPER FUNCTIONS FOR MAIN THREAD                        *
 ******************************************************************************/

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
 * Handles a request.
 *
 * A request is handled by adding the request to the alarm list.
 *
 * Note that the alarm list mutex must be locked by the caller of this method
 * (because it updates the alarm list).
 */
void handle_request(alarm_request_t *alarm_request) {
    /*
     * Insert alarm request to alarm list
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
}

/**
 * Handles a request in a thread-safe way. This is done by locking the alarm
 * list mutex, handling the request, then unlocking the alarm list mutex.
 *
 * A request is handled by adding the request to the alarm list.
 */
void handle_request_thread_safe(alarm_request_t *alarm_request) {
    /*
     * Lock mutex
     */
    pthread_mutex_lock(&alarm_list_mutex);

    /*
     * Handle request
     */
    handle_request(alarm_request);

    /*
     * Unlock mutex
     */
    pthread_mutex_unlock(&alarm_list_mutex);
}

/*******************************************************************************
 *                                 MAIN THREAD                                 *
 ******************************************************************************/

/**
 * Main thread.
 *
 * The main thread is responsible for creating one alarm thread and one consumer
 * thread, receiving and parsing user input into alarm requests, and adding
 * alarm requests to the alarm list.
 */
int main() {
    char input[USER_INPUT_BUFFER_SIZE]; // Buffer to store user input.

    alarm_request_t *alarm_request;     // Most recent alarm request (data
                                        // structure representing the user's
                                        // request).

    pthread_t alarm_thread;             // Alarm thread.

    pthread_t consumer_thread;          // Consumer thread.

    DEBUG_PRINT_START_MESSAGE();

    /*
     * Create alarm thread.
     */
    pthread_create(&alarm_thread, NULL, alarm_thread_routine, NULL);

    DEBUG_MESSAGE("Alarm thread created");

    /*
     * Create consumer thread.
     */
    pthread_create(&consumer_thread, NULL, consumer_thread_routine, NULL);

    DEBUG_MESSAGE("Consumer thread created");

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

