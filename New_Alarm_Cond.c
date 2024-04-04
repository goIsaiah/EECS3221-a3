#include <pthread.h>
#include <stdbool.h>
#include "errors.h"
#include "types.h"
#include "debug.h"
#include "Command_Parser.h"
#include <semaphore.h>

#define USER_INPUT_BUFFER_SIZE 256
#define CIRCULAR_BUFFER_SIZE 4

#define MAIN_THREAD_ID 1
#define ALARM_THREAD_ID 2
#define CONSUMER_THREAD_ID 3
#define PERIODIC_DISPLAY_THREAD_START_ID 4

/*******************************************************************************
 *      HELPER FUNCTIONS FOR MODIFYING LISTS (USED BY DIFFERENT THREADS)       *
 ******************************************************************************/

/**
 * A.3.2. Inserts the alarm request in its specified position in the alarm list
 * (sorted by their time values).
 */
void insert_to_alarm_list(alarm_request_t *list_header, alarm_request_t *alarm_request) {
    alarm_request_t *current = list_header;
    alarm_request_t *next = list_header->next;

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
 * A.3.3.2. Removes all alarm requests with the given alarm id from the list of
 * alarms.
 *
 * Note that THIS METHOD WILL FREE ALARM REQUESTS THAT ARE FOUND, so don't keep
 * references to the alarm list entries.
 *
 * Note that the alarm list mutex MUST BE LOCKED by the caller of this method.
 */
void remove_alarm_requests_from_list(alarm_request_t *list_header, int alarm_id) {
    alarm_request_t *alarm_node = list_header->next;
    alarm_request_t *alarm_prev = list_header;
    alarm_request_t *alarm_temp;

    /*
     * Keeps on searching the list until it finds the correct ID
     */
    while (alarm_node != NULL) {
        if (alarm_node->alarm_id == alarm_id) {
            /*
             * We have found an alarm request with the given ID, so  remove it
             * from the list and free it.
             */
            alarm_prev->next = alarm_node->next;

            alarm_temp = alarm_node;
            alarm_node = alarm_node->next;
            free(alarm_temp);

            /*
             * Don't increment alarm node because it has already been
             * incremented during the removal above.
             */
            continue;
        }

        alarm_node = alarm_node->next;
        alarm_prev = alarm_prev->next;
    }
}

/**
 * A.3.3.3. Removes all alarm requests with the given alarm id from the list of
 * alarms, except for the given alarm request. The time value of the old alarm
 * request that was removed is returned (so that the alarm thread can use it to
 * check for periodic display threads). If no alarms were removed, then -1 is
 * returned.
 *
 * Note that THIS METHOD WILL FREE ALARM REQUESTS THAT ARE FOUND, so don't keep
 * references to the alarm list entries.
 *
 * Note that the alarm list mutex MUST BE LOCKED by the caller of this method.
 */
int remove_old_alarm_requests_from_list(alarm_request_t *list_header, int alarm_id, alarm_request_t *newest_alarm_request) {
    alarm_request_t *alarm_node = list_header->next;
    alarm_request_t *alarm_prev = list_header;
    alarm_request_t *alarm_temp;
    int old_time_value = -1;

    /*
     * Keeps on searching the list until it finds the correct ID
     */
    while (alarm_node != NULL) {
        if (alarm_node->alarm_id == alarm_id) {
            /*
             * We have found an alarm request with the given ID. If it is not
             * the most recent alarm request, then remove it from the list and
             * free it. Also save the old request's time value so that it can be
             * returned.
             */
            if (alarm_node != newest_alarm_request) {
                old_time_value = alarm_node->time;
                alarm_prev->next = alarm_node->next;

                alarm_temp = alarm_node;
                alarm_node = alarm_node->next;
                free(alarm_temp);

                /*
                 * Don't increment alarm node because it has already been
                 * incremented during the removal above.
                 */
                continue;
            }
        }

        alarm_node = alarm_node->next;
        alarm_prev = alarm_prev->next;
    }

    return old_time_value;
}

/*******************************************************************************
 *      DATA SHARED BETWEEN CONSUMER THREAD AND PERIODIC DISPLAY THREADS       *
 ******************************************************************************/
/**
 * Header of the list of alarms for the alarm display list.
 */
alarm_request_t alarm_display_list_header = {0};

/**
 * The number of readers (peroidic display threads) currently reading from the
 * alarm display list.
 */
int reader_count = 0;

/**
 * Semaphore for readers (periodic display threads) updating the reader count.
 */
sem_t reader_count_sem;

/**
 * Semaphore for controlling access to the alarm display list.
 *
 * This should solve the reader-writer problem. Only one thread should have this
 * semaphore locked at a time. If it is a writer thread (consumer), then other
 * writer threads should not be allowed to lock the semaphore. If it is a reader
 * thread (periodic display thread) it should allow other readers to access the
 * alarm display list, but should not allow the writer to acces the alarm
 * display list.
 */
sem_t alarm_display_list_sem;

/*******************************************************************************
 *                           PERIODIC DISPLAY THREAD                           *
 ******************************************************************************/

/**
 * A.3.5. Periodic display thread.
 */
void *periodic_display_thread_routine(void *arg) {
    periodic_display_thread_t *thread = ((periodic_display_thread_t*) arg);

    DEBUG_PRINTF("Periodic display thread %d running.\n", thread->thread_id);

    return NULL;
}

/*******************************************************************************
 *           DATA SHARED BETWEEN CONSUMER THREAD AND ALARM THREAD              *
 ******************************************************************************/

/**
 * Circular buffer used to store alarms.
 */
alarm_request_t* circularBuffer[CIRCULAR_BUFFER_SIZE] = {0};

/**
 * Size of the circular buffer used to store alarms. 
 */
int circularBufferSize = 0;

/**
 * Integer used to keep track of where to write 
 */
int writeIndex = 0;

/**
 * Integer used to keep track of which index in the circular buffer to read from
 */
int readIndex = 0;

/**
 * Mutex controlling access to the circular buffer. Any thread that updates or
 * reads from the circular buffer must have this mutex locked.
 */
pthread_mutex_t circular_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Semaphore representing the number of empty spaces in the buffer.
 *
 * This semaphore should be initialized to the size of the circular buffer
 * because the buffer is initially empty.
 *
 * Whenever an item is added to the circular buffer, the semaphore value
 * decreases. This means the producer (alarm thread) will call `wait` on this
 * semaphore. Once the semaphore value reaches 0, producers threads will block
 * until a consumer thread consumes an item from the buffer and calls `signal`
 * on this semaphore.
 */
sem_t circular_buffer_empty_sem;

/**
 * Semaphore representing the number of full spaces in the buffer.
 *
 * This semaphore should be initialized to 0 because the buffer is initially
 * empty.
 *
 * Whenever an item is consumed from the circular buffer, the semaphore value
 * decreases. This means the consumer (consumer thread) will call `wait` on this
 * semaphore. Once the semaphore value reaches 0, consumer threads will block
 * until a producer thread adds an item to the buffer and calls `signal` on this
 * semahpore.
 */
sem_t circular_buffer_full_sem;

/*******************************************************************************
 *                     HELPER FUNCTIONS FOR CONSUMER THREAD                    *
 ******************************************************************************/

/**
 * A.3.4.5. Prints the contents of the circular buffer.
 *
 * This uses the read index as the start and the write index as the end, and
 * iterates through the elements in between, printing each one. This works
 * because when an alarm request is added to the buffer the write index is
 * incremented, and when an alarm is taken from the buffer the read index is
 * incremented.
 *
 * Note that THE MUTEX FOR THE CIRCULAR BUFFER MUST BE LOCKED by the caller of
 * this method.
 */
void print_circular_buffer() {
    alarm_request_t *alarm_request;

    printf("[");

    for (int i = readIndex; i != writeIndex; i = (i + 1) % CIRCULAR_BUFFER_SIZE) {
        if (circularBuffer[i] != NULL) {
            alarm_request = circularBuffer[i];

            printf(
                "{Index: %d, AlarmId: %d, Type: %s, Time: %d, Message: %s}",
                i,
                alarm_request->alarm_id,
                request_type_string(alarm_request),
                alarm_request->time,
                alarm_request->message
            );

            if ((i + 1) % CIRCULAR_BUFFER_SIZE != writeIndex) {
                printf(", ");
            }
        }
    }

    printf("]\n");
}

/**
 * Gets an item from the circular buffer. This will wait on a semaphore for
 * requests to consume if the buffer is empty.
 */
alarm_request_t *get_item_from_circular_buffer() {
    /*
     * Wait on the full semaphore. If there are no elements in the buffer
     * (semaphore value is 0), then this call will block until an item is added
     * to the buffer and this semaphore is signaled.
     */
    sem_wait(&circular_buffer_full_sem);

    /*
     * Lock the circular buffer mutex to ensure mututal exclusion on the buffer.
     */
    pthread_mutex_lock(&circular_buffer_mutex);

    /*
     * Get item from buffer
     */
    alarm_request_t *alarm_request = circularBuffer[readIndex];

    /*
     * Remove item from buffer
     */
    circularBuffer[readIndex] = NULL;

    /*
     * Increment read index
     */
    readIndex = (readIndex + 1) % CIRCULAR_BUFFER_SIZE;

    /*
     * Unlock the circular buffer mutex to allow other threads to access the
     * buffer.
     */
    pthread_mutex_unlock(&circular_buffer_mutex);

    /*
     * Signal the empty semaphore to signal that there is one more empty spot in
     * the buffer.
     */
    sem_post(&circular_buffer_empty_sem);

    return alarm_request;
}

/**
 * Consume the alarm request that was retrieved from the circular buffer.
 */
void consume_alarm_request(alarm_request_t *alarm_request) {
    /*
     * Save alarm ID in case the alarm request is freed
     */
    int alarm_id = alarm_request->alarm_id;

    /*
     * Take action depending on the type of the alarm request
     */
    switch (alarm_request->type) {
        case Start_Alarm:
            /*
             * A.3.4.2. Insert alarm request into alarm display list
             */
            insert_to_alarm_list(&alarm_display_list_header, alarm_request);

            /*
             * A.3.4.2. Print message that alarm request has been inserted
             * into alarm display list
             */
            printf(
                "Consumer Thread has Inserted Alarm_Request_Type %s "
                "Request(%d) at %ld: Time = %d Message = %s into Alarm "
                "Display List.\n",
                request_type_string(alarm_request),
                alarm_id,
                time(NULL),
                alarm_request->time,
                alarm_request->message
            );

            break;

        case Change_Alarm:
            /*
             * A.3.4.3. Remove old requests with the same alarm ID
             */
            remove_old_alarm_requests_from_list(
                &alarm_display_list_header,
                alarm_request->alarm_id,
                alarm_request
            );

            /*
             * A.3.4.3. Insert alarm request into alarm display list
             */
            insert_to_alarm_list(&alarm_display_list_header, alarm_request);

            /*
             * A.3.4.3. Print message that old alarm requests have been
             * removed and new alarm request has been inserted into alarm
             * display list
             */
            printf(
                "Consumer Thread %d at %ld has Removed All Previous Alarm "
                "Requests With Alarm ID %d From Alarm Display List and Has"
                "Inserted Retrieved Change Alarm Request(%d) Time = %d "
                "Message = %s into Alarm Display List.\n",
                CONSUMER_THREAD_ID,
                time(NULL),
                alarm_id,
                alarm_id,
                alarm_request->time,
                alarm_request->message
            );

            break;

        case Cancel_Alarm:
            /*
             * A.3.4.4. Remove alarm requests from alarm display list
             */
            remove_alarm_requests_from_list(&alarm_display_list_header, alarm_id);

            /*
             * A.3.4.4. Print message that alarm requests have been
             * cancelled and removed from the alarm display list
             */
            printf(
                "Consumer Thread %d Has Cancelled and Removed All Alarm "
                "Requests With Alarm ID (%d) from Alarm Display List at "
                "%ld.\n",
                CONSUMER_THREAD_ID,
                alarm_id,
                time(NULL)
            );

            break;

        default:
            printf("Consumer thread found error: invalid alarm request type!\n");
            return;
    }

}

/*******************************************************************************
 *                             CONSUMER THREAD                                 *
 ******************************************************************************/

/**
 * A.3.4. Consumer thread.
 */
void *consumer_thread_routine(void *arg) {
    DEBUG_MESSAGE("Consumer thread running.");

    alarm_request_t *alarm_request;

    while (1) {
        /*
         * Get an alarm request from the circular buffer
         */
        alarm_request = get_item_from_circular_buffer();

        /*
         * A.3.4.1. Print message that an alarm request has been retrieved from
         * the circular buffer
         */
        printf(
            "Consumer Thread has Retrieved Alarm_Request_Type %s Request(%d) "
            "at %ld: Time = %d Message = %s from Circular_Buffer Index: %d\n",
            request_type_string(alarm_request),
            alarm_request->alarm_id,
            time(NULL),
            alarm_request->time,
            alarm_request->message,
            (readIndex + (CIRCULAR_BUFFER_SIZE - 1)) % CIRCULAR_BUFFER_SIZE
        );

        DEBUG_PRINT_ALARM_REQUEST(alarm_request);

        /*
         * Lock the circular buffer mutex to ensure mututal exclusion on the
         * buffer.
         */
        pthread_mutex_lock(&circular_buffer_mutex);

        /*
         * A.3.4.5. Print the contents of the circular buffer
         */
        print_circular_buffer();

        /*
         * Unlock the circular buffer mutex to allow other threads to access the
         * buffer.
         */
        pthread_mutex_unlock(&circular_buffer_mutex);
    }

    return NULL;
}

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
 *                      DATA SPECIFIC TO ALARM THREAD                          *
 ******************************************************************************/

/**
 * Header of the list of periodic display threads.
 */
periodic_display_thread_t thread_list_header = {0};

/**
 * The number of periodic display threads that the alarm thread has created.
 */
int number_of_periodic_display_threads = 0;

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
 * A.3.3.4. Checks if any alarm requests in the alarm list have the given time
 * value.
 *
 * Returns true if there exists at least one alarm request in the alarm list
 * that has the given time value, false otherwise.
 */
bool does_time_exist_in_alarm_list(int time) {
    alarm_request_t *alarm_node = alarm_list_header.next;

    while (alarm_node != NULL) {
        if (alarm_node->time == time) {
            return true;
        }

        alarm_node = alarm_node->next;
    }

    return false;
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
 * A.3.3.5. Adds an alarm to the circular buffer.
 *
 * This is the producer part of the bounded-buffer problem. It waits on the
 * empty semaphore to decrement it, adds the item to the buffer, then signals
 * the full semaphore to increment it. A third semaphore controls access to the
 * buffer. This also increments the write index.
 */
void write_to_circular_buffer(alarm_request_t *alarm_request) {
    /*
     * Wait on the emptry semaphore. If the buffer is full (semaphore value is
     * 0), then this call will block until an item is taken from the buffer and
     * this semaphore is signaled.
     */
    sem_wait(&circular_buffer_empty_sem);

    /*
     * Lock the circular buffer mutex to ensure mututal exclusion on the
     * buffer.
     */
    pthread_mutex_lock(&circular_buffer_mutex);

    /*
     * A.3.3.5. Put the alarm request in the circular buffer
     */
    circularBuffer[writeIndex] = alarm_request;

    /*
     * Increment the write index
     */
    writeIndex = (writeIndex + 1) % CIRCULAR_BUFFER_SIZE;

    /*
     * Unlock the circular buffer mutex to allow other threads to access the
     * buffer.
     */
    pthread_mutex_unlock(&circular_buffer_mutex);

    /*
     * Signal the full semaphore to signal that there is one more item in the
     * buffer.
     */
    sem_post(&circular_buffer_full_sem);
}

/**
 * A.3.3.4. Checks if a thread in the thread list exists for the given time
 * value.
 *
 * Returns true if a thread with that time value does exist in the thread list,
 * false otherwise.
 */
bool does_thread_exist(int time) {
    periodic_display_thread_t *thread = thread_list_header.next;

    while (thread != NULL) {
        if (thread->time == time) {
            return true;
        }

        thread = thread->next;
    }

    return false;
}

/**
 * Adds a thread to the thread list.
 */
void add_thread_to_list(periodic_display_thread_t *thread) {
    if (thread_list_header.next == NULL) {
        // If the thread list is empty, make the new thread the only element in
        // the list.
        thread_list_header.next = thread;
    } else {
        // If the thread list is not empty, insert the new thread as the first
        // element of the list.
        thread->next = thread_list_header.next;
        thread_list_header.next = thread;
    }
    // Increment number of periodic display threads
    number_of_periodic_display_threads++;
}

/**
 * Removes all threads with the given time value from the list of threads.
 *
 * Note that THIS METHOD WILL FREE THREAD DATA THAT ARE FOUND, so don't keep
 * references to the thread list entries.
 */
void remove_thread_from_list(int time) {
    periodic_display_thread_t *thread_node = thread_list_header.next;
    periodic_display_thread_t *thread_prev = &thread_list_header;
    periodic_display_thread_t *thread_temp;

    while (thread_node != NULL) {
        if (thread_node->time == time) {
            thread_prev->next = thread_node->next;

            thread_temp = thread_node;
            thread_node = thread_node->next;
            free(thread_temp);

            // Decrement number of periodic display threads
            number_of_periodic_display_threads--;

            /*
             * Don't increment alarm node because it has already been
             * incremented during the removal above.
             */
            continue;
        }

        thread_node = thread_node->next;
        thread_prev = thread_prev->next;
    }
}

/**
 * A.3.3.4. Creates a new periodic display thread and adds the data
 * representation of the thread to the thread list.
 */
void create_periodic_display_thread(alarm_request_t *alarm_request) {
    /*
     * Allocate data for the new thread
     */
    periodic_display_thread_t *thread = malloc(sizeof(periodic_display_thread_t));
    if (thread == NULL) {
        errno_abort("Malloc failed");
    }

    /*
     * Give time value and ID for the new thread
     */
    thread->time = alarm_request->time;
    thread->thread_id = PERIODIC_DISPLAY_THREAD_START_ID
        + number_of_periodic_display_threads;

    /*
     * A.3.3.4. Create the new periodic display thread
     */
    pthread_create(
        &thread->thread,
        NULL,
        periodic_display_thread_routine,
        thread
    );

    /*
     * Add the newly-created thread to the list of threads
     */
    add_thread_to_list(thread);

    /*
     * A.3.3.4. Print success message
     */
    printf(
        "Alarm Thread Created New Periodic display thread %d For Alarm(%d) at "
        "%ld: For New Time Value = %d Message = %s\n",
        thread->thread_id,
        alarm_request->alarm_id,
        time(NULL),
        alarm_request->time,
        alarm_request->message
    );
}

/**
 * Make a copy of an alarm request.
 *
 * Note that the alarm request that is returned is malloced, so it must be freed
 * later.
 *
 * This function is useful because we may need to delete alarm requests in the
 * alarm list while the consumer thread still needs references to those alarm
 * requests.
 */
alarm_request_t *copy_alarm_request(alarm_request_t *alarm_request) {
    /*
     * Allocate memory for the copy of the alarm request
     */
    alarm_request_t *alarm_request_copy = malloc(sizeof(alarm_request_t));
    if (alarm_request_copy == NULL) {
        errno_abort("Malloc failed");
    }

    /*
     * Fill in data
     */
    alarm_request_copy->alarm_id = alarm_request->alarm_id;
    alarm_request_copy->type = alarm_request->type;
    alarm_request_copy->time = alarm_request->time;
    strncpy(
        alarm_request_copy->message,
        alarm_request->message,
        strlen(alarm_request->message)
    );
    alarm_request_copy->creation_time = alarm_request->creation_time;
    alarm_request_copy->next = NULL;

    return alarm_request_copy;
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
    int newest_alarm_id = newest_alarm_request->alarm_id;

    int old_time_value;

    /*
     * Make a copy of the alarm request to give to the consumer thread
     */
    alarm_request_t *alarm_request_copy = copy_alarm_request(newest_alarm_request);

    /*
     * Take action depending on the type of the alarm request
     */
    switch (newest_alarm_request->type) {
        case Start_Alarm:
            /*
             * A.3.3.4. If no thread in the thread list exists for the time
             * value of the alarm request, then create a periodic display thread
             * to handle requests with that time value.
             */
            if (does_thread_exist(newest_alarm_request->time) == false) {
                create_periodic_display_thread(newest_alarm_request);
            }

            break;

        case Change_Alarm:
            /*
             * A.3.3.3.  Remove old alarm requests from list
             */
            old_time_value = remove_old_alarm_requests_from_list(
                &alarm_list_header,
                newest_alarm_id,
                newest_alarm_request
            );

            /*
             * If there are no longer any alarm requests with the given time
             * value in the alarm list, then remove the periodic display thread
             * corresponding to that time value.
             *
             * Note that this does not destory the thread, just the data
             * corresponding to it.
             */
            if (does_time_exist_in_alarm_list(old_time_value) == false) {
                remove_thread_from_list(old_time_value);
            }

            /*
             * A.3.3.3. Print success message.
             */
            printf(
                "Alarm Thread %d at %ld Has Removed All Alarm Requests "
                "With Alarm ID %d From Alarm List Except The Most Recent "
                "Change Alarm Request(%d) Time = %d Message = %s\n",
                0,
                time(NULL),
                newest_alarm_id,
                newest_alarm_id,
                newest_alarm_request->time,
                newest_alarm_request->message
            );

            /*
             * A.3.3.4. If no thread in the thread list exists for the time
             * value of the alarm request, then create a periodic display thread
             * to handle requests with that time value.
             */
            if (does_thread_exist(newest_alarm_request->time) == false) {
                create_periodic_display_thread(newest_alarm_request);
            }

            break;

        case Cancel_Alarm:
            /*
             * Save time value of the alarm request before deleting the alarm
             * requests for this alarm ID
             */
            old_time_value = newest_alarm_request->time;

            /*
             * A.3.3.2. Remove alarm requests from list with the given alarm ID
             */
            remove_alarm_requests_from_list(&alarm_list_header, newest_alarm_id);

            /*
             * A.3.3.2. Print success message
             */
            printf(
                "Alarm Thread %d Has Cancelled and Removed All Alarm Requests "
                "With Alarm ID %d from Alarm List at %ld\n",
                0,
                newest_alarm_id,
                time(NULL)
            );

            /*
             * If there are no longer any alarm requests with the given time
             * value in the alarm list, then remove the periodic display thread
             * corresponding to that time value.
             *
             * Note that this does not destory the thread, just the data
             * corresponding to it.
             */
            if (does_time_exist_in_alarm_list(old_time_value) == false) {
                remove_thread_from_list(old_time_value);
            }

            break;

        default:
            printf("Alarm thread found error: invalid alarm request type!\n");
            return;
    }

    /*
     * A.3.3.5. Add the alarm request to the circular buffer
     */
    write_to_circular_buffer(alarm_request_copy);

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
    insert_to_alarm_list(&alarm_list_header, alarm_request);

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
     * Initialize the circular buffer empty semaphore to the size of the buffer.
     */
    sem_init(&circular_buffer_empty_sem, 0, CIRCULAR_BUFFER_SIZE);

    /*
     * Initialize the circular buffer full semaphore to 0.
     */
    sem_init(&circular_buffer_full_sem, 0, 0);

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

