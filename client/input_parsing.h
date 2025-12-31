/*
* ***************************************************************
* 
* Header containing input parsing utilities.
* 
* ***************************************************************
*/

#pragma once
#include <WinSock2.h>
#include <stdint.h>
#include "client_utils.h"
#include "msg_handling.h"

typedef struct header header;
typedef struct active_names active_names;


/*
* Parses the given input string, Creates a message header based on the input, sets the action function pointer
* to the corresponding action based on the input string's command, or if the command is local, executes it immediately
* and sets the action function pointer to NULL.
* 
* Parameters:
* @hdr [out] -> Pointer to the header to be filled out based on the input.
* 
* @buf [in, out] -> Pointer to the buffer containing the input string, which the message body will be copied to after the call,
* if the message has a trivial body (is not a file, and can therefore be sent in one piece, without being fragmented).
* 
* @anames [in] -> Pointer to the anames struct, containing the local list of active user names, if it exists.
* 
* @uname [in] -> The client's username.
* 
* @action [out] -> Pointer to the action function pointer, to be set depending on the input command.
* 
* Return value:
* If the input string is not formatted properly, the function will return ENONFATAL(2), if the command in the input string is local
* and doesn't send any messages, the function will return REPEAT(2), which is an alias for ENONFATAL(2). If the input string contains
* the disconnect command, the function will return SIGQUIT(3), otherwise, the function returns OK(0).
*/
u_clfunc_status interpret_input(header* hdr, char* buf, const active_names* anames, const char* uname, afptr* action);