/*
* **************************************************************************************
* 
* Header interpreting and message routing/request handling operations.
* 
* **************************************************************************************
*/
#pragma once
#include <WinSock2.h>

typedef struct cl_param_struct cl_param_struct;

typedef struct header header;

typedef struct status_context status_context;

typedef struct client client;

//Routing function pointer, representing the signature of each routing function.
typedef status_context(*routing_fptr)(header*, SOCKET, SOCKET, cl_param_struct*, char*, status_context);


/*
* Interprets the header, ensuring it follows a legal format, sets the rcpt pointer to the recipient of the message (if there is one),
* returns the corresponding routing function based on the type of the message. The function is daisy-chained, meaning that it will
* return immediately if an unhandled error has previously occured before calling the function.
* 
* Parameters:
* 
* @m_header [in] -> Pointer to the header to be interpreted.
* 
* @clparams [in] -> Pointer to the clparams struct containing all the relevant client-handling parameters.
* 
* @snd [in] -> Pointer to the client representing the sender.
* 
* @rcpt [out] -> Pointer to the client representing the recipient, to be filled out by the function (if applicable).
* 
* @stc [in, out] -> Pointer to the status context struct to be examined before the function is executed, and to be filled out
* in case an error occurs in the function itself.
* 
* Return value: Pointer to the corresponding routing function of the signature status_context(*)(header*, SOCKET, SOCKET, cl_param_struct*, char*, status_context) if
* no error occurs, otherwise NULL.
*/
routing_fptr s_interpret_header(header* m_header, cl_param_struct* clparams, client** snd, client** rcpt, status_context* stc);


/*
* Attempts to send an amount of data specified by len through the socket sock from the buffer buf, depending on whether the socket is temporarily or permanently blocking,
* or non-blocking it may behave differently, but with temporarily blocking sockets, it is either guaranteed to send all data or fail in the process, returning the number of
* bytes successfully sent and updating the status context struct with the status and socket error that caused the function to fail. The function is daisy-chained, meaning that
* it will return immediately if there a previously-unhandled error occured before the function call, reflected in the status context struct.
* 
* Parameters:
* 
* @sock [in] -> A valid, connected socket to send the data to.
* 
* @buf [in] -> Buffer containing the data to be sent.
* 
* @len [in] -> Amount of data to be sent.
* 
* @scon [in, out] -> The status context struct to be examined and modified if necessary.
* 
* Return value: The number of bytes successfully sent.
*/
int32_t s_send_all(SOCKET sock, const void* buf, uint32_t len, status_context* scon);


/*
* Attempts to receive an amount of data specified by len from the socket sock to the buffer buf, depending on whether the socket is temporarily or permanently blocking,
* or non-blocking it may behave differently, but with temporarily blocking sockets, it is either guaranteed to receive all data or fail in the process, returning the number of
* bytes successfully read and updating the status context struct with the status and socket error that caused the function to fail. The function is daisy-chained, meaning that
* it will return immediately if there a previously-unhandled error occured before the function call, reflected in the status context struct.
* 
* Parameters:
* 
* @sock [in] -> A valid, connected socket to read the data from.
* 
* @buf [out] -> Buffer to write the incoming data.
* 
* @len [in] -> Amount of data expected to be received.
* 
* @scon [in, out] -> The status context struct to be examined and modified if necessary.
* 
* Return value: The number of bytes successfully read.
*/
int32_t s_recv_all(SOCKET sock, void* buf, uint32_t len, status_context* scon);


/*
* Receives the fixed-width message header from the sender. The function is daisy chained, meaning
* it will return immediately if a previously unhandled error has occured before the function was called,
* as reflected in the status context struct.
* 
* Parameters:
* 
* @sock [in] -> A valid, connected socket to receive data from.
* 
* @m_header [out] -> Pointer to the header struct to be filled out.
* 
* @status [in, out] -> Status context struct to be examined and modified if necessary.
* 
* Return value: The modified or unmodified status context struct, note that upon failure the struct's status
* will be set to INCOMPL_HEADER_RECV
*/
status_context s_get_header(SOCKET snd, header* m_header, status_context status);


/*
* Routes a header, followed by the file from the sender to the recipient, using chunked file transfer with confirmation between each sent chunk. Before 
* receiving each chunk, the server sends a control chunk header to the sender, allowing the ability to signal to the sender if the
* recipient has disconnected, to abolish the file transfer without having to wait for the timeout. Note that both clients need to adhere
* to this for it to work as intended. So the control chunk header is first sent with the relevant flags, after which the sender sends a chunk header
* containing the amount of data to be expected in the upcoming chunk, followed by said chunk of data. After that the server forwards both the chunk header
* and the chunk body to the recipient, if this is successfuly received the server sends a control header to the sender again, rinse and repeat. The function is
* daisy chained meaning it will return immediately if a previously unhandled error has occured before calling the function, as reflected in the status context
* struct and will return early if an error occurs in the function.
* 
* Parameters:
* 
* @m_header [in] -> Header containing information about the message (including information about the file to be routed).
* 
* @snd [in] -> A valid, connected socket from which to receive data.
* 
* @rcpt [in] -> A valid, connected socket which to send data to.
* 
* @clparams [optional, unused] -> An unused parameter for purpose of maintaining function signature. NULL may safely be passed.
* 
* @buf [out] -> Buffer used for temporarily storing received chunks for forwarding, must be at least 64 KB in size.
* 
* @stcon [in] -> Status context struct to be examined and modified if necessary.
* 
* Return value: The modified or unmodified status context struct containing context relating to the point of failure in the call chain.
*/
status_context s_route_file(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon);


/*
* Receives a message from the socket snd and forwards it to the socket rcpt, preceded by the header. The function is
* daisy chained meaning it will return immediately if a previously unhandled error has occured before calling the function, 
* as reflected in the status context struct and will return early if an error occurs in the function.
* 
* Parameters:
* 
* @m_header [in] -> Pointer to the header containing relevant information about the message, to be routed.
* 
* @snd [in] -> A valid, connected socket from which to receive data.
* 
* @rcpt [in] -> A valid, connected socket which to send data to.
* 
* @clparams [optional, unused] -> An unused parameter for the purpose of maintaining compatibility with the routing function pointer signature.
* NULL may safely be passed.
* 
* @buf [out] -> Buffer used for receiving and forwarding the message body.
* 
* @stcon [in] -> Status context struct to be examined and modified if necessary.
* 
* Return value: The modified or unmodified status context struct containing context relating to the point of failure in the call chain.
*/
status_context s_route_message(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon);


/*
* Receives a server request from the sender, processes it and sends back the response if the request was valid. The function is daisy chained meaning it will 
* return immediately if a previously unhandled error has occured before calling the function, as reflected in the status context
* struct and will return early if an error occurs in the function. The function is daisy chained meaning it will return immediately 
* if a previously unhandled error has occured before calling the function, as reflected in the status context struct and will return 
* early if an error occurs in the function.
* 
* @m_header [in, out] -> Header containing basic request info, to be modified for the response.
* 
* @snd [in] -> A valid, connected socket representing the request sender and response recipient.
* 
* @rcpt [optional, unused] -> Unused parameter used simply for function pointer signature compatibility, doesn't need to be a valid socket.
* 
* @clparams [in, out] -> Pointer to the clparams struct containing all the relevant client-handling parameters for use in processing requests.
* 
* @buf [in, out] -> Pointer to the buffer containing the request body, later used for storing the response body before sending.
* 
* @stcon [in] -> Status context struct to be examined and modified if necessary. Note that an invalid request yields a status of MALFORMED_REQUEST.
* 
* Return value: The modified or unmodified status context struct containing context relating to the point of failure in the call chain.
*/
status_context s_handle_request(header* m_header, SOCKET snd, SOCKET rcpt, cl_param_struct* clparams, char* buf, status_context stcon);


/*
* Copies the names of all reachable users into the buffer (entire 32-byte username buffer, not just the length of the username string), skipping
* users already marked as disconnected, testing connectivity to each user before adding their name to the buffer, marking them as disconnected
* if they can't be reached.
* 
* Parameters:
* 
* @buf [out] -> Buffer to put the usernames of the active clients in, must be at least as big as the total number of users * UNAME_MAXSIZE(32) to
* be large enough to accomodate all usernames.
* 
* @clparams[in, out] -> Pointer to the struct containing all the relevant parameters for client-handling.
* 
* Return value: Total amount of data copied to the buffer (always congruent with 0 mod UNAME_MAXSIZE).
*/
uint32_t s_add_active_names(char* buf, cl_param_struct* clparams);