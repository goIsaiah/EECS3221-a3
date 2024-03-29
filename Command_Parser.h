#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

/**
 * Parses a request as a string (from user input) into an alarm_request_t
 * object.
 *
 * If this function returns NULL, then the user request was invalid and could
 * not be parsed. Otherwise, this function will return a pointer to the alarm
 * request.
 *
 * Note that the alarm_request_t pointer that is returned is malloced, so it must
 * be freed when it is done being used.
 */
alarm_request_t *parse_request(char input[]);

#endif

