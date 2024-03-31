#include <pthread.h>
#include "errors.h"
#include "types.h"
#include "debug.h"
#include "Command_Parser.h"
#include <semaphore.h>

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
 *                      HELPER FUNCTIONS FOR ALARM THREAD                      *
 ******************************************************************************/

/**
 * A.3.3.1 Returns a pointer to the most recently-added alarm request in the
 * alarm list. If the alarm list is empty, then NULL is returned.
 *
 * This is done by traversing the entire alarm list and finding the alarm
 * request with the greated creation time (most recently created).
 *
 * Note that the alarm list mutex must be locked by the caller of this method.
 */
alarm_request_t *get_most_recent_alarm_request() {
    alarm_request_t *alarm_request = alarm_list_header.next;
    alarm_request_t *newest_alarm_request = alarm_list_header.next;

    while (alarm_request != NULL) {
        if (alarm_request->creation_time > newest_alarm_request->creation_time) {
            newest_alarm_request = alarm_request;
        }

        alarm_request = alarm_request->next;
    }

    return newest_alarm_request;
}

/**
 * A.3.3.2., A.3.3.3. Removes all alarm requests with the given alarm id from
 * the list of alarms.
 *
 * Note that THIS METHOD WILL FREE ALARM REQUESTS THAT ARE FOUND, so don't keep
 * references to the alarm list entries.
 *
 * Note that the alarm list mutex MUST BE LOCKED by the caller of this method.
 */
void remove_old_alarm_requests_from_list(int id, alarm_request_t *newest_alarm_request) {
    alarm_request_t *alarm_node = alarm_list_header.next;
    alarm_request_t *alarm_prev = &alarm_list_header;
    alarm_request_t *alarm_temp;

    /*
     * Keeps on searching the list until it finds the correct ID
     */
    while (alarm_node != NULL) {
        if (alarm_node->alarm_id == id) {
            /*
             * We have found an alarm request with the given ID. If it is not
             * the most recent alarm request, then remove it from the list and
             * free it.
             */
            if (alarm_node != newest_alarm_request) {
                alarm_prev->next = alarm_node->next;

                alarm_temp = alarm_node;
                alarm_node = alarm_node->next;
                free(alarm_temp);
            }
        }

        alarm_node = alarm_node->next;
        alarm_prev = alarm_prev->next;
    }
}

/**
 * A.3.3.6. Prints all the alarm requests in the alarm list. Note that the alarm
 * list is sorted by the time values of the alarm requests, so the alarm
 * requests will be printed in order of time values.
 */
void print_alarm_list() {
    alarm_request_t *alarm_request = alarm_list_header.next;

    printf("[");

    while (alarm_request != NULL) {
        printf(
            "{AlarmId: %d, Type: %s, Time: %d, Message: %s}",
            alarm_request->alarm_id,
            request_type_string(alarm_request),
            alarm_request->time,
            alarm_request->message
        );
        if (alarm_request->next != NULL) {
            printf(", ");
        }

        alarm_request = alarm_request->next;
    }

    printf("]\n");
}

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

void handle_alarm_list_update() {
    /*
     * Make sure alarm list is not empty
     */
    if (alarm_list_header.next == NULL) {
        printf("Alarm thread found error: alarm list is empty!\n");
        return;
    }

    /*
     * A.3.3.1 Get the most recent alarm request
     */
    alarm_request_t *newest_alarm_request = get_most_recent_alarm_request();

    /*
     * Take action depending on the type of the alarm request
     */
    switch (newest_alarm_request->type) {
        case Start_Alarm:
            printf("test!!!\n");
            break;

        case Change_Alarm:
            /*
             * A.3.3.3.  Remove old alarm requests from list
             */
            remove_old_alarm_requests_from_list(
                newest_alarm_request->alarm_id,
                newest_alarm_request
            );

            printf(
                "Alarm Thread %d at %ld Has Removed All Alarm Requests "
                "With Alarm ID %d From Alarm List Except The Most Recent "
                "Change Alarm Request(%d) Time = %d Message = %s\n",
                0,
                time(NULL),
                newest_alarm_request->alarm_id,
                newest_alarm_request->alarm_id,
                newest_alarm_request->time,
                newest_alarm_request->message
            );

            // Check if any thread has this time value, if not then create it

            break;

        case Cancel_Alarm:
            /*
             * Remove old alarm requests from list
             */
            remove_old_alarm_requests_from_list(
                newest_alarm_request->alarm_id,
                newest_alarm_request
            );

            printf(
                "Alarm Thread %d Has Cancelled and Removed All Alarm Requests "
                "With Alarm ID %d from Alarm List at %ld\n",
                0,
                newest_alarm_request->alarm_id,
                time(NULL)
            );
            break;
        default:
            printf("Alarm thread found error: invalid alarm request type!\n");
            return;
    }

    /*
     * A.3.3.6. Print all the alarm requests currently in the alarm list
     */
    print_alarm_list();
}

/*******************************************************************************
 *                               ALARM THREAD                                  *
 ******************************************************************************/

/**
 * A.3.3. Alarm thread.
 */
void *alarm_thread_routine(void *arg) {
    DEBUG_MESSAGE("Alarm thread running.");

    /*
     * Lock the alarm list mutex
     */
    pthread_mutex_lock(&alarm_list_mutex);

    while (1) {
        /*
         * A.3.3.1. Wait for changes to the alarm list
         */
        pthread_cond_wait(&alarm_list_cond, &alarm_list_mutex);

        /*
         * Handle the update to the alarm list
         */
        handle_alarm_list_update();

        /*
         * Unlock alarm list mutex
         */
        pthread_mutex_unlock(&alarm_list_mutex);
    }

    return NULL;
}

/*******************************************************************************
 *                     HELPER FUNCTIONS FOR MAIN THREAD                        *
 ******************************************************************************/

/**
 * A.3.2. Inserts the alarm request in its specified position in the alarm list
 * (sorted by their time values).
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
        } 
        else {
            current = next;
            next = next->next;
        }
    }

    // Insert at the end of the list
    current->next = alarm_request;
    alarm_request->next = NULL;
}

/**
 * Finds an alarm in the list using a specified ID
 *
 * Alarm list has to be locked by the caller of this method
 *
 * Goes through the linked list, when specified ID is found, pointer
 * to that alarm will be returned.
 *
 * If the specified ID is not found, return NULL.
 */
alarm_request_t* find_alarm_by_id(int id) {
    alarm_request_t *alarm_node = alarm_list_header.next;
    
    //Loop through the list
    while(alarm_node != NULL) {
        //if ID is found, return pointer to it
        if(alarm_node-> alarm_id == id) {
            return alarm_node;
        }
        //if ID is NOT found, go to next node
        else {
            alarm_node = alarm_node->next;
        }
    }
    //If the entire list was searched and specified ID was not
    //found, return NULL.
    return NULL;
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
     * Get alarm requests with the given ID from the alarm list
     */
    alarm_request_t *old_alarm_request = find_alarm_by_id(alarm_request->alarm_id);

    /*
     * If the request was a Start_Alarm request, make sure there is not already
     * an existing alarm request with that same ID.
     */
    if (alarm_request->type == Start_Alarm && old_alarm_request != NULL) {
        printf(
            "Alarm with ID %d already exists, so request type Start_Alarm "
            "cannot be performed\n",
            alarm_request->alarm_id
        );
        return;
    }

    /*
     * If the request was not a Start_Alarm request, make sure there is already
     * an existing alarm request with that same ID.
     */
    if (alarm_request->type != Start_Alarm && old_alarm_request == NULL) {
        printf(
            "Alarm with ID %d does not exist, so request type %s cannot be "
            "performed on alarm ID %d\n",
            alarm_request->alarm_id,
            request_type_string(alarm_request),
            alarm_request->alarm_id
        );
        return;
    }

    /*
     * If the alarm request is a Cancel_Alarm request, then it will not have the
     * time and message values from the user. In this case, copy the time and
     * message values from the older request from the alarm list.
     */
    if (alarm_request->type == Cancel_Alarm) {
        alarm_request->time = old_alarm_request->time;
        strncpy(
            alarm_request->message,
            old_alarm_request->message,
            strlen(old_alarm_request->message)
        );
    }

    /*
     * A.3.2. Insert alarm request to alarm list
     */
    insert_to_alarm_list(alarm_request);

    /*
     * A.3.2. Print success message
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
     * Signal the alarm thread to wake up
     */
    pthread_cond_broadcast(&alarm_list_cond);

    /*
     * Unlock mutex
     */
    pthread_mutex_unlock(&alarm_list_mutex);
}

/*******************************************************************************
 *                                 MAIN THREAD                                 *
 ******************************************************************************/

/**
 * A.3.2. Main thread.
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
     * A.3.2. Create alarm thread.
     */
    pthread_create(&alarm_thread, NULL, alarm_thread_routine, NULL);

    DEBUG_MESSAGE("Alarm thread created");

    /*
     * A.3.2. Create consumer thread.
     */
    pthread_create(&consumer_thread, NULL, consumer_thread_routine, NULL);

    DEBUG_MESSAGE("Consumer thread created");

    while (1) {
        printf("Alarm > ");

        /*
         * A.3.2. Get a request from user input. If NULL, then the user did not
         * enter a command.
         */
        if (fgets(input, USER_INPUT_BUFFER_SIZE, stdin) == NULL) {
            printf("Bad command\n");
            continue;
        }

        // Replace newline with null terminating character
        input[strcspn(input, "\n")] = 0;

        /*
         * A.3.2. Parse user's request.
         */
        alarm_request = parse_request(input);

        /*
         * A.3.2. If alarm_request is NULL, then the request was invalid.
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

