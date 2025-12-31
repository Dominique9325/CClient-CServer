/*
* ***************************************************************************
* 
* Functions, data structures and enums relating to server error handling.
* 
* ***************************************************************************
*/

#pragma once

#include <WinSock2.h>

typedef struct cl_param_struct cl_param_struct;

typedef struct client client;

typedef struct header header;


/*
* Struct containing the necessary error-handling context, including the failure status,
* socket error code, whether the status has been converted from a low-level to high level
* status (i.e. s_recv_all is a low level function, it's called by s_route_message, which is
* a higher level function, in case of an error, it converts the low-level status returned by
* s_recv_all into a context-dependent high-level status specific to the type of high-level function),
* package type (message (T_MSG) or file (T_FILE), message by default) and routing mode (whether 
* the failure happend while attempting to receive data or send it).
* 
* Note: By design, functions using this struct should be daisy-chained, returning early if the status
* is not OK, to preserve the context at the site of where the error actually occured.
* 
* @err -> socket error (WinSock2)
* 
* @status -> the current status
* 
* @conv_mode -> whether the context was converted from a low-level to a high-level one
* 
* @package_type -> the current type of package being processed
* 
* @routing_mode -> RECV, SEND or UNDEFINED (if not applicable at the moment)
*/
typedef struct status_context
{
	// general
	int32_t err;
	uint8_t status;

	// high level context
	uint8_t conv_mode : 1;
	uint8_t package_type : 1;

	// low level context
	uint8_t routing_mode : 2;
}status_context;


/*
* Statuses (or stati, however you would say) relating to a socket, mainly used in low-level functions.
* 
* @CONN_OK -> the same as any other OK status, no error has occured
* 
* @INV_SOCKET -> the socket passed to a function was not a valid socket
* 
* @CONN_CLOSED_GRACEFULLY -> the connection on the socket was remotely closed
* 
* @CONN_CLOSED -> the connection was abruptly closed on the remote-side
* 
* @TIMED_OUT -> the connection has timed out
* 
* @POLL_TIMED_OUT -> Conntest (or WSAPoll) has returned without any events of interest or errors occuring
* 
* @OTHER_ERR -> Any WinSock2 error not covered
* 
* @NONE_AVAIL -> No (suitable) clients left
*/
enum socket_status
{
	CONN_OK,
	INV_SOCKET,
	CONN_CLOSED_GRACEFULLY,
	CONN_CLOSED,
	TIMED_OUT,
	POLL_TIMED_OUT,
	OTHER_ERR,
	NONE_AVAIL
};


/*
* High level statuses (or stati) typically used in high-level functions, relating to individual
* requests/messages rather than individual WinSock2 function calls.
* 
* @MSG_OK -> no error has occured
* 
* @INCOMPL_RECV -> Sender has disconnected before delivering the full message.
* 
* @INCOMPL_HEADER_RECV -> Sender has disconnected before delivering the message header.
* 
* @INCOMPL_SEND -> Recipient has disconnected before receiving the full message.
* 
* @CL_NOT_FOUND -> No recipient with the corresponding username could be found.
* 
* @MALFORMED_HEADER -> Illegal message header format.
* 
* @MALFORMED_REQUEST -> Illegal server-directed request format.
*/
enum message_status
{
	MSG_OK,
	INCOMPL_RECV = 8U,
	INCOMPL_HEADER_RECV,
	INCOMPL_SEND,
	CL_NOT_FOUND,
	MALFORMED_HEADER,
	MALFORMED_REQUEST,
};


/*
* Statuses (or stati) relating to client functions (mostly unused now).
* 
* @AOK -> Function completed successfully.
* 
* @BAD_CALL -> Function call failed either due to illegal argument or the requested
* client not being found.
*/
enum clfunc_status
{
	AOK,
	BAD_CALL = 15U,
};


/*
* Conversion mode of the status context.
* 
* @UNCONV -> Low-level, original.
* 
* @CONV -> High-level, converted by a high-level function.
*/
enum conv_mode
{
	UNCONV,
	CONV
};


/*
* Routing mode in the low-level function.
* 
* @UNDEFINED -> not applicable (i.e. conntest)
* 
* @SEND -> sending mode, meaning s_send_all was the function that failed.
* 
* @RECV -> receiving mode, meaning s_recv_all or s_get_header was the function that failed.
*/
enum curr_routing_mode
{
	UNDEFINED,
	SEND,
	RECV
};


/*
* Reset type for the reset_status function.
* 
* @RST_ERR -> Only resets the status and the socket error.
* 
* @RST_ALL -> Completely resets the status context struct.
*/
enum status_reset_type
{
	RST_ERR,
	RST_ALL
};


/*
* Type of the package currently being routed.
* 
* @T_MSG -> The package is a text-message.
* 
* @T_FILE -> The package is a file.
*/
enum package_type
{
	T_MSG,
	T_FILE
};


/*
* Converts a WinSock2 socket error to the corresponding low-level status.
* 
* Parameters:
* 
* @err [in] -> WinSock2 error code.
* 
* Return value: Low-level status corresponding to the WinSock2 error.
*/
uint8_t wserror_to_status(int32_t err);


/*
* Converts a low-level status context to a high level status context if an error has occured
* and the status has not previously been converted.
* 
* Parameters:
* 
* @stcon [in] -> status context struct to convert
* 
* @hstatus [in] -> high level status value to set if status is not OK.
* 
* @pkgtype [in] -> package type to set if the status is not OK.
* 
* Return value: The converted status context struct (if the status is not OK and it has not
* been previously converted).
*/
status_context ltho_stcontext(status_context stcon, uint8_t hstatus, uint8_t pkgtype);


/*
* Creates a default-initialized status context struct (zeroed out).
* 
* Return value: The created status context struct.
*/
status_context create_stcontext();


/*
* Resets the status context either partially or fully depending on the
* value of reset_type. Partial reset means only the error and the status
* get cleared, while a full reset means the struct is zeroed out.
* 
* Parameters:
* 
* @scon [out] -> Pointer to the status context struct to be reset.
* 
* @reset_type [in] -> enum value specifying the type of reset
*/
void reset_status(status_context* scon, uint8_t reset_type);


/*
* Handles the error that occurs during the call chain of daisy-chained functions if one occurs, as well as errors resulting from
* failing to handle said errors. For example if the recipient disconnects while receiving the message, that's an INCOMPL_SEND error,
* the handling procedure would be to tell the sender the recipient has disconnected and letting them send another message to someone else,
* but if the sender were to disconnect too, now that's an additional error which must be handled. In said case a new suitable client must be found
* as the holder of the media access token (the right to send a message), if a suitable client cannot be found for any reason, then that's an irrecoverable
* error and it cannot be handled. After the call, if the error is handled successfuly, rcpt will always point to the client who received the latest message,
* the client who should be expected to send the next message. In case of an unknown error, an error without a defined handler, the function will print
* the error code to stdout.
* 
* Parameters:
* 
* @clparams [in, out] -> Pointer to the struct containing all the relevant client-handling parameters.
* 
* @hdr [out] -> Pointer to the header to be prepared internally by error-handlers for sending error-handling messages.
* 
* @snd [in, out] -> Double pointer to the client who was the sender when the error occured (if the sender was not the server), set to NULL by the function.
* 
* @rcpt [in, out] -> Double pointer to the client who was the recipient when the error occured (set to the new recipient by the function).
* 
* @buf [out] -> Buffer used to store the error-handling messages formatted and sent by the server.
* 
* @scon [in, out] -> Pointer to the status context struct which should contain information about the relevant context in which the error has occured to be used
* for error handling. Status set to NONE_AVAIL if no suitable recipient is found (a fatal error).
*/
void s_handle_error(cl_param_struct* clparams, header* hdr, client** snd, client** rcpt, char* buf, status_context* scon);