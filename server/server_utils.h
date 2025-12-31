/*
* ***************************************************************************************************************************
* 
* Small server utility/helper functions, as well as debugging macros for use exclusively by the server due to the context.
* 
* ***************************************************************************************************************************
*/

#pragma once
#include <WinSock2.h>
#include <inttypes.h>

#ifndef NDEBUG

//Uses the formatted DEBUG_PRINT to print "Function will return immediately." if the status in the status context is not OK.
#define IMM_RET(st) if ((st)->status) DEBUG_PRINT("Function will return immediately.")

//Takes a pointer to a client struct, if it's not null, it expands to the client's name, otherwise it expands to the string literal "NONE"
#define CLI_NULLABLE(cl) (cl) ? ((cl)->clname) : "NONE"
#define ACCEPTWAITTIMEOUT -1

#else
#define ACCEPTWAITTIMEOUT 10000
#define IMM_RET(st)
#define CLI_NULLABLE(cl)
#endif

typedef struct status_context status_context;

typedef struct header header;

/*
* Swaps the addresses pointed to by the 2 provided pointers.
* Warning: The two pointers should point to the same-width type.
* 
* Parameters:
* 
* @a [in, out] -> First pointer to be swapped.
* 
* @b [in, out] -> Second pointer to be swapped;
*/
void uswap(void** a, void** b);


/*
* Checks if a buffer is truncated or not (first element being null-terminator).
* 
* Parameters:
* 
* @buf [in] -> Buffer to be examined.
* 
* Return value: 1 if the buffer is not truncated, otherwise 0.
*/
uint8_t is_present(char* buf);


/*
* Checks whether the header is properly formatted for a server request (may not contain a file,
* must have the request flag toggled, if it's a prod request the body mustn't be empty, etc.
* 
* @m_header [in] -> Header to be examined.
* 
* Return value: 1 if the header is formatted properly for a server request, otherwise 0.
*/
uint8_t is_valid_request(header* m_header);


/*
* Checks whether the header is properly formatted for a file transfer (file flag must be toggled,
* mustn't have any request flags toggled, mustn't have an empty body, etc.).
* 
* Parameters:
* 
* @m_header [in] -> Header to be examined.
* 
* Return value: 1 if the header is formatted properly for a file transfer, otherwise 0.
*/
uint8_t is_valid_file_message(header* m_header);


/*
* Checks whether the header is properly formatted for a regular message (mustn't have any request flags
* toggled, body mustn't be empty, etc.).
* 
* Parameters:
* 
* @m_header [in] -> Header to be examined.
* 
* Return value: 1 if the header is formatted properly for a regular message, otherwise 0.
*/
uint8_t is_valid_text_message(header* m_header);


/*
* Checks whether the upcoming message is a server request (checks whether FLG_REQ is toggled and whether
* the recipient is named "server".
* 
* Parameters:
* 
* @hdr [in] -> Header to be examined.
* 
* Return value: 1 if it is a request, otherwise 0.
*/
uint8_t isrequest(const header* hdr);


/*
* Comparator function for sequentially ordering clients in the client reference array (to be used by sorting algorithms, namely qsort).
* Clients are ordered by their ordinal number, removed references (NULL) are placed at the end of the array.
* 
* Parameters:
* 
* @a [in] -> First comparand (client).
* 
* @b [in] -> Second comparand (client).
* 
* Return value: Standard in all qsort comparation functions, 0 if the two comparands are equal, a negative integer if a < b, otherwise
* a positive integer.
*/
int32_t order_comp(const void* a, const void* b);


/*
* Wrapper around WSAPoll, polls the provided socket for the events provided in the flags parameter, afterwards setting flags to
* the return events (revents) if the poll hasn't timed out. Note, once again, that the flags parameter is overwritten after calling
* the function. Depending on the result of the polling, the status context struct may be updated if the desired event doesn't happen.
* 
* Parameters:
* 
* @sock [in] -> A valid, connected socket to be polled.
* 
* @timeout [in] -> How long the function should block waiting for an event to happen.
* 
* @flags [in, out] -> Requested events to be polled for, later set to the return events.
* 
* @opcontext [in] -> Status context struct to be potentially modified and returned.
* 
* Return value: The modified or unmodified status context struct.
*/
status_context conntest(SOCKET sock, int32_t timeout, uint16_t* flags, status_context opcontext);


/*
* Converts the status enum value to a string literal.
* 
* Parameters:
* 
* @stcon [in] -> Status context struct containing the status to be converted.
* 
* Return value: String literal representing the status enum value, "Unknown status"
* if the status is not a valid status enum value.
*/
char* status_enumval_tostr(status_context stcon);


/*
* Converts the routing mode enum value to the corresponding string literal.
* 
* Parameters:
* 
* @stcon [in] -> Status context struct with the routing mode to be converted.
* 
* Return value: String literal corresponding to the routing mode enum value,
* if it's not a valid routing mode enum value, then the string literal
* "Unknown routing mode".
*/
char* routingmode_enumval_tostr(status_context stcon);


/*
* Converts the package mode enum value to the corresponding string literal.
* 
* Parameters:
* 
* @stcon [in] -> Status context s truct with the package type enum value to be converted.
* 
* Return value: String literal corresponding to the package type enum value, if an invalid
* enum value is passed, it's treated as a message by default.
*/
char* packagetype_enumval_tostr(status_context stcon);