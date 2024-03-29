#include "errors.h"
#include "types.h"
#include <regex.h>

#define MAXIMUM_MESSAGE_SIZE 128

/**
 * This is the data type that holds information about parsing a
 * request. It contains the type of the request, the regular
 * expression for the request, and the number of matches within the
 * request (that must be parsed out).
 */
typedef struct regex_parser {
    request_type type;
    const char *regex_string;
    int expected_matches;
} regex_parser;

/**
 * These are the regexes for the requests that we must parse.
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
 * This method takes a string and checks if it matches any of the request
 * formats. If there is no match, NULL is returned. If there is a match, it
 * parses the string into a request and returns it.
 *
 * Note that the alarm_request_t pointer that is returned was malloced, so it
 * must be freed when it is done being used.
 */
alarm_request_t *parse_request(char input[]) {
    regex_t regex; // Holds the regex that we are testing.

    int re_status; // Holds the status of regex tests.

    regmatch_t matches[4]; // Holds the number of matches. (No request has more
                           // than 3 matches, which we add 1 to because the
                           // entire string itself counts as a match. This is
                           // why the array size is 4).

    alarm_request_t *alarm_request; // Holds the pointer to the request that
                                    // will be returned. (This will be malloced,
                                    // so it must be freed later).

    int number_of_regexes = 3; // Three regexes for three request types.

    char alarm_id_buffer[64]; // Buffer used to hold alarm_id as a string when
                              // it is being converted to an int.

    char time_buffer[64]; // Buffer used to hold time as a string when it is
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
            REG_EXTENDED
        );

        // Make sure regex compilation succeeded
        if (re_status != 0) {
            fprintf(
                stderr,
                "Regex %d \"%s\" did not compile\n",
                i,
                regexes[i].regex_string
            );
            exit(1);
        }

        /*
         * Test input string against regex.
         */
        re_status = regexec(
            &regex,
            input,
            regexes[i].expected_matches,
            matches,
            0
        );

        if (re_status == REG_NOMATCH) {
            /*
             * If no match, then free the regex and continue on with the loop.
             */
            regfree(&regex);
            continue;
        } else if (re_status != 0) {
            /*
             * If regex test returned nonzero value, then it was an error. In
             * this case, free the regex and stop the program.
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
             * In this case (regex returned 0), we know the input has matched a
             * request and we can now parse it.
             */

            // Free the regex
            regfree(&regex);

            /*
             * Allocate alarm request (IT MUST BE FREED LATER!).
             */
            alarm_request = malloc(sizeof(alarm_request_t));
            if (alarm_request == NULL) {
                errno_abort("Malloc failed");
            }

            /*
             * Fill command with data.
             */

            // Get the type from the regex that succeeded
            alarm_request->type = regexes[i].type;

            // Get the alarm_id from the input (if it exists)
            if (regexes[i].expected_matches > 1) {
                strncpy(
                    alarm_id_buffer,
                    input + matches[1].rm_so,
                    matches[1].rm_eo - matches[1].rm_so
                );
                alarm_request->alarm_id = atoi(alarm_id_buffer);
            } else {
                alarm_request->alarm_id = 0;
            }

            // Get the time from the input (if it exists)
            if (regexes[i].expected_matches > 2) {
                strncpy(
                    time_buffer,
                    input + matches[2].rm_so,
                    matches[2].rm_eo - matches[2].rm_so
                );
                alarm_request->time = atoi(time_buffer);
            } else {
                alarm_request->time = 0;
            }

            // Copy the message from the input (if it exists)
            if (regexes[i].expected_matches > 3) {
                strncpy(
                    alarm_request->message,
                    input + matches[3].rm_so,
                    MAXIMUM_MESSAGE_SIZE
                );
                alarm_request->message[matches[3].rm_eo - matches[3].rm_so] = 0;
            } else {
                strncpy(alarm_request->message, "", MAXIMUM_MESSAGE_SIZE);
            }

            return alarm_request;
        }
    }

    return NULL;
}

