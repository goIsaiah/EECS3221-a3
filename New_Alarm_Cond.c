/**
 * This is the data type that holds information about parsing a
 * command. It contains the type of the command, the regular
 * expression for the command, the number of matches within the
 * command (that must be parsed out).
 */
typedef struct regex_parser {
    command_type type;
    const char *regex_string;
    int expected_matches;
} regex_parser;

/**
 * These are the regexes for the commands that we must look for.
 */
regex_parser regexes[] = {
    {
        Start_Alarm,
        "Start_Alarm\\(([0-9]+)\\):[[:space:]]([0-9]+)[[:space:]](.*)",
        4
    },
    {
        Change_Alarm,
        "Change_Alarm\\(([0-9]+)\\):[[:space:]]([0-9]+)[[:space:]](.*)",
        4
    },
    {
        Cancel_Alarm,
        "Cancel_Alarm\\(([0-9]+)\\)",
        2
    }
};

/**
 * Header of the list of alarms.
 */
alarm_t alarm_header = {0, 0, "", NULL, false, 0, 0};

/**
 * Mutex for the alarm list. Any thread reading or modifying the alarm list must
 * have this mutex locked.
 */
pthread_mutex_t alarm_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Condition variable for the alarm list. This allows threads to wait for
 * updates to the alarm list.
 */
pthread_cond_t alarm_list_cond = PTHREAD_COND_INITIALIZER;

/**
 * Header of the list of threads.
 */
thread_t thread_header = {0,0,0,NULL, 0};

/**
 * Mutex for the thread list. Any thread reading or modifying the thread list
 * must have this mutex locked.
 */
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * The current event being handled. If the event is NULL, then there is no
 * event being handled. Note that there can only be one event being handled at
 * any given time.
 */
event_t *event = NULL;

/**
 * Mutex for the event. Any thread reading or modifying the event must have
 * this mutex locked.
 */
pthread_mutex_t event_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * This method takes a string and checks if it matches any of the
 * command formats. If there is no match, NULL is returned. If there
 * is a match, it parses the string into a command and returns it.
 */
command_t *parse_command(char input[]) {
    regex_t regex; // Holds the regex that we are testing

    int re_status; // Holds the status of regex tests

    regmatch_t matches[4]; // Holds the number of matches. (No command has more
                           // than 3 matches, which we add 1 to because the
                           // string itself counts as a match).

    command_t *command; // Holds the pointer to the command that will be
                        // returned. (This will be malloced, so it must be
                        // freed later).

    int number_of_regexes = 6; // Six regexes for six commands

    char alarm_id_buffer[64]; // Buffer used to hold alarm_id as a string when
                              // it is being converted to an int.

    char time_buffer[64]; // Buffer used to  hold time as a string when it is
                          // being converted to an int.

    /*
     * Loop through the regexes and test each one.
     */
    for (int i = 0; i < number_of_regexes; i++) {
        /*
         * Compile the regex
         */
        re_status = regcomp(
            &regex,
            regexes[i].regex_string,
            REG_EXTENDED);

        // Make sure regex compilation succeeded
        if (re_status != 0) {
            fprintf(
                stderr,
                "Regex %d \"%s\" did not compile\n",
                i,
                regexes[i].regex_string);
            exit(1);
        }

        /*
         * Test input string against regex
         */
        re_status = regexec(
            &regex,
            input,
            regexes[i].expected_matches,
            matches,
            0);

        if (re_status == REG_NOMATCH) {
            /*
             * If no match, then free the regex and continue on with
             * the loop.
             */
            regfree(&regex);
            continue;
        }
        else if (re_status != 0) {
            /*
             * If regex test returned nonzero value, then it was an
             * error. In this case, free the regex and stop the
             * program.
             */
            fprintf(
                stderr,
                "Regex %d \"%s\" did not work with input \"%s\"\n",
                i,
                regexes[i].regex_string,
                input);
            regfree(&regex);
            exit(1);
        }
        else {
            /*
             * In this case (regex returned 0), we know the input has
             * matched a command and we can now parse it.
             */

            // Free the regex
            regfree(&regex);

            /*
             * Allocate command (IT MUST BE FREED LATER)
             */
            command = malloc(sizeof(command_t));
            if (command == NULL) {
                errno_abort("Malloc failed");
            }

            /*
             * Fill command with data
             */
            command->type = regexes[i].type;
            // Get the alarm_id from the input (if it exists)
            if (regexes[i].expected_matches > 1) {
                strncpy(
                    alarm_id_buffer,
                    input + matches[1].rm_so,
                    matches[1].rm_eo - matches[1].rm_so);
                command->alarm_id = atoi(alarm_id_buffer);
            }
            else {
                command->alarm_id = 0;
            }
            // Get the time from the input (if it exists)
            if (regexes[i].expected_matches > 2) {
                strncpy(
                    time_buffer,
                    input + matches[2].rm_so,
                    matches[2].rm_eo - matches[2].rm_so);
                command->time = atoi(time_buffer);
            }
            else {
                command->time = 0;
            }
            // Copy the message from the input (if it exists)
            if (regexes[i].expected_matches > 3) {
                strncpy(
                    command->message,
                    input + matches[3].rm_so,
                    matches[3].rm_eo - matches[3].rm_so);
                command->message[matches[3].rm_eo - matches[3].rm_so] = 0;
            }

            return command;
        }
    }

    return NULL;
}



/*
typedef struct barrier_tag {
    pthread_mutex_t     mutex;
    pthread_cond_t      cv;
    int                 valid;
    int                 threshold;
    int                 counter;
    int                 cycle;
} barrier_t;

#define BARRIER_VALID 0xdbcafe

#define BARRIER_INITIALIZER(cnt) \
    {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, \
    BARRIER_VALID, cnt, cnt, 0}

int barrier_init(barrier_t *barrier, int count) {
    int status;

    barrier->threshold = barrier->counter = count;
    barrier->cycle = 0;
    status = pthread_mutex_init(&barrier->mutex, NULL);
    if (status != 0) {
        return status;
    }
    status = pthread_cond_init(&barrier->cv, NULL);
    if (status != 0) {
        pthread_mutex_destroy(&barrier->mutex);
        return status;
    }
    barrier->valid = 
}
*/