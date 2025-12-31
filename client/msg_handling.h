/*
* **********************************************************************************************************
* 
* Header with functions for handling messages based on the header (both receiving and sending). Contains
* handler functions for sending and receiving of all types of messages.
* 
* **********************************************************************************************************
*/

#pragma once
#include <WinSock2.h>
#include <stdint.h>
#include "client_utils.h"

typedef struct active_names active_names;

typedef struct header header;


//Action function pointer - a pointer to a procedure for handling a given message type.
typedef u_clfunc_status(*afptr)(SOCKET, header*, char*, active_names**);


//Path to the designated folder for receiving incoming files, the folder must already exist.
extern char ffolder[MAX_PATH];

/*
* Interprets the received header - toggles the token flag on or off based on the contents
* of the header bitmask, returns a function pointer to an appropriate procedure for handling
* the given message type.
* 
* Parameters:
* @sock [in] -> An open and connected socket, connected to the server or a remote peer.
* 
* @hdr [in] -> Pointer to a header struct to be read and interpreted.
* 
* @tokenflag [in] -> Pointer to the token flag to be toggled based on the contents of the header.
* 
* Return value: Function pointer to the appropriate procedure for handling the type of message. NULL
* if there is no such function.
*/
afptr cl_interpret_header(SOCKET sock, header* hdr, uint8_t* tokenflag);


/*
* Receives the amount of chunked data as specified in the header, putting it into a newly created file with
* the same name as in the header struct. If a file with the same name already exists, it will append "(1)" to
* the end of the file name (as many times as necessary). If the FLG_DISCARD_MSG chunk header flag is toggled, the
* entire file will automatically be discarded. Note: displays a progress bar of the download.
* 
* Parameters:
* @sock [in] -> An open and connected socket either to the server or directly to a remote peer.
* 
* @hdr [in] -> Pointer to a header struct associated with the given message.
* 
* @buf [out] -> Pointer a pre-allocated buffer to hold individual chunks temporarily.
* Must be at least 64 KB in capacity.
* 
* @_unused [optional, unused] -> A parameter necessary to satisfy the action function pointer format.
* It's unused, it may be NULL.
* 
* Return value: OK(0) if the operation is successful or the failure is handled gracefully by an intermediary (server),
* EFATAL(1) if an issue that suggests that the closest connection point is broken and cannot be recovered, ENONFATAL(2) in case
* of any other failure.
*/
u_clfunc_status recv_file(SOCKET sock, header* hdr, char* buf, active_names** _unused);


/*
* Receives the amount of data specified in the header, printing it out as a message to stdout along with the username
* of the sender.
* 
* Parameters:
* @sock [in] -> An open and connected socket either to hte server or directly to a remote peer.
* 
* @hdr [in] -> Pointer to a header struct associated with the given message.
* 
* @buf [out] -> Pointer to a pre-allocated buffer for receiving the message, must be sufficiently large to hold the amount
* of data specified in the provided header struct.
* 
* @_unused [optional, unused] -> A parameter necessary to satisfy the action function pointer format.
* It's unused, it may be NULL.
* 
* Return value: OK(0) on success, EFATAL(1) on fatal failure, ENONFATAL(2) on non-fatal failure.
*/
u_clfunc_status recv_msg(SOCKET sock, header* hdr, char* buf, active_names** _unused);


/*
* Receives a response from the server (reponse to a prior request) and appropriately handles it. If it's a connectivity prod, it
* prints out whether the given peer is connected and reachable or not, if it's a list of active names, it copies them to the provided
* active names struct.
* 
* Parameters:
* @sock [in] -> A socket that is open and connected to the server.
* 
* @hdr [in] -> Pointer to the header struct associated with the given response.
* 
* @buf [out] -> Pointer to a pre-allocated buffer for receiving the request. Must be large enough to accomodate the amount of data specified
* in provided header struct.
* 
* @anames [out] -> A double pointer to the active_names struct, the direct pointer may be NULL or point to an active names struct. In either case the address of a heap-allocated
* struct will be assigned to it, containing all the active names. The freeing of this memory is the caller's responsibility.
* 
* Return value: OK(0) on success, EFATAL(1) on fatal failure, ENONFATAL(2) on non-fatal failure.
*/
u_clfunc_status recv_s_resp(SOCKET sock, header* hdr, char* buf, active_names** anames);


/*
* Sends the provided header, followed by the message body contained in buf, if the message has a body through the socket.
* 
* Parameters:
* @sock [in] -> A valid, connected socket representing the nearest/direct connection point.
* 
* @hdr [in] -> Pointer to a header struct containing metadata about the message.
* 
* @buf [in] -> Pointer to the buffer with the message body.
* 
* @_unused [optional, unused] -> An unused parameter that exists only for compatibility, NULL may be passed.
* 
* Return value:
* On fatal failure, which would suggest the loss of connection to the direct/immediate connection point, the function
* returns EFATAL(1). On non-fatal failure which merely suggests any non-fatal, non-specific issue has occured, the function
* returns ENONFATAL(2), otherwise, if there is no error, the function returns OK(0).
*/
u_clfunc_status send_msg(SOCKET sock, header* hdr, char* buf, active_names** _unused);


//Just an alias for send_msg because the same function suffices both for sending requests and messages.
#define send_req send_msg


/*
* Sends the provided header, followed by the file specified in the header. If the file is larger than 64 KB it shall be divided into 64 KB chunks.
* The biggest supported file size is 4 GB. Before sending each chunk, it expects confirmation from the nearest connection point to continue sending
* the file. Send progress is shown by a loading bar.
* 
* @sock [in] -> A valid, connected socket representing the nearest/direct connection point.
* 
* @hdr [in] -> Header containing the metadata relevant to the message, including the file size. FLG_FILE should be toggled.
* 
* @buf [out] -> Pointer to the buffer to copy the file to before sending (in chunks if necessary). Must be at least as big as specified by BUF_SIZE,
* which should be 65535 by default.
* 
* @_unused [optional, unused] -> An unused parameter, NULL may be passed safely.
* 
* Return value:
* On fatal failure, which would suggest the loss of connection to the direct/immediate connection point, the function
* returns EFATAL(1). On non-fatal failure which merely suggests any non-fatal, non-specific issue has occured, the function
* returns ENONFATAL(2), otherwise, if there is no error, the function returns OK(0).
*/
u_clfunc_status send_file(SOCKET sock, header* hdr, char* buf, active_names** _unused);