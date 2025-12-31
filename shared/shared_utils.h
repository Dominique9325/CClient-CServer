/*
* ***************************************************************************************************************************
* 
* Header with structs, enums, functions and macros which are shared both by the server and the client. The functions here are 
* almost exclusively utility functions, being either relatively thin wrappers around existing functions for the sake of 
* convenience or functions with relatively niche, but useful functionality with the goal of avoiding boilerplate code in 
* the scope of the caller.
* ***************************************************************************************************************************
*/

#pragma once
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <WinSock2.h>

typedef struct status_context status_context;

//Auxiliary macros available in debug mode, along with altered, infinite timeouts for debugging purposes.
#ifndef NDEBUG

#define MSG_RECV_TIMEOUT 0
#define MSG_SND_TIMEOUT 0

//Wrapper around printf that adds additional format containing the file, function and line of code from which the macro was called. Used for debugging.
#define DEBUG_PRINT(fmt, ...) printf("\nDEBUG::%s::%s::%d::( \""fmt"\" )\n", strrchr(__FILE__,'\\') + 1, __func__, __LINE__, ##__VA_ARGS__)

#define HDR_RCPT_PRESENT(hdr) ((hdr)->recipient[0] != '\0' ? (hdr)->recipient : "NONE")
#define HDR_SND_PRESENT(hdr) ((hdr)->sender[0] != '\0' ? (hdr)->sender : "NONE")
#define HDR_FILE_PRESENT(hdr) ((hdr)->file_name[0] != '\0' ? (hdr)->file_name : "NONE")
#define HDR_SIZE(hdr, mode) (mode) == 'c' ? ntohl((hdr)->size) : (hdr)->size
#define HDR_FIELDS(hdr, mode) (hdr)->msg_flags, HDR_SND_PRESENT((hdr)), HDR_RCPT_PRESENT((hdr)), HDR_FILE_PRESENT((hdr)), HDR_SIZE((hdr),(mode))
#define HDR_FMT "Flags: %"PRIu8", Sender: %s, Recipient: %s, File: %s, Size: %"PRIu32

//Prints the contents of a given header, either with the host byte-order or the network byte-order size depending on the mode parameter.
#define DEBUG_PRINT_HDR(hdr, mode) DEBUG_PRINT(HDR_FMT, HDR_FIELDS((hdr), (mode)))

#define CHHDR_FMT "Flags: %"PRIu8", Chunk size: %"PRIu16
#define CHHDR_SIZE(chhdr, mode) (mode) == 'c' ? ntohs((chhdr)->size) : (chhdr)->size
#define CHHDR_FIELDS(chhdr, mode) (chhdr)->flags, CHHDR_SIZE((chhdr), (mode))

//Prints the contents of a given chunk header, either with host byte-order or the network byte-order size depending on the mode parameter.
#define DEBUG_PRINT_CHHDR(chhdr, mode) DEBUG_PRINT(CHHDR_FMT, CHHDR_FIELDS((chhdr), (mode)))

#else

#define MSG_RECV_TIMEOUT 2000
#define MSG_SND_TIMEOUT 2000
#define DEBUG_PRINT(fmt, ...)
#define HDR_RCPT_PRESENT(hdr)
#define HDR_SND_PRESENT(hdr)
#define HDR_FILE_PRESENT(hdr)
#define HDR_SIZE(hdr, mode)
#define HDR_FIELDS(hdr, mode)
#define HDR_FMT
#define DEBUG_PRINT_HDR
#define CHHDR_FMT
#define CHHDR_SIZE
#define CHHDR_FIELDS
#define DEBUG_PRINT_CHHDR

#endif

#define MAX_FILENAME_LEN 256
#define UNAME_MAXSIZE 32
#define BUF_SIZE 65535
#pragma pack(push, 1)


/*
* The fixed-size struct which always preceeds a message, containing imporant
* metadata related to the upcoming message. Since the struct is meant
* to be sent to a remote peer, compiler data alignment for this struct
* is turned off.
* 
* Members:
* @msg_flags -> A bitmask containing flags relevant to the nature and context
* of the message.
* 
* @sender -> The username of the sender of the message.
* 
* @recipient -> The username of the recipient of the message.
* 
* @file_name (optional) -> Name of the file being transferred (if there is one).
* 
* @size -> the total size of the body of the message (whether it be text message, file or request).
* minimum legal size is 1 (a single null-terminator).
*/
typedef struct header
{
	uint8_t msg_flags;
	char sender[32];
	char recipient[32];
	char file_name[256];
	uint32_t size;
}header;


/*
* The fixed-size struct which preceeds each chunk of data sent, but
* mainly only in file transfers. It may also be used as a control unit
* to abolish file transfers on either end. Since the struct is meant to
* be sent to a remote peer, compiler data alignment for this struct is
* turned off.
* 
* Members:
* 
* @size -> Size of the chunk being sent (legal range 0 to 65,536). If
* the value is 0, then the chunk header represents a control header.
* 
* @flags (optional) -> A bitmask containing the flags relevant to the
* status of the transfer. The flags are typically toggled only when
* a problem arises in the transfer.
*/
typedef struct ch_header
{
	uint16_t size;
	uint8_t flags;
}ch_header;

#pragma pack(pop)

/*
* All the flags that make up the flags bitmask in the chunk header (ch_header) struct:
* 
* FLG_DCON(1) -> The other party/client has disconnected in the middle of a transfer.
* 
* FLG_DISCARD_MSG(2) -> the partially received file (if the recipient of the header is the
* recipient of the message) is a binary file and can therefore be discarded.
*/
enum ch_hdr_flags
{
	FLG_DCON = 0x1U,
	FLG_DISCARD_MSG = 0x1U << 1,
};


/*
* All the flags that make up the msg_flag bitmask in the header struct:
* 
* FLG_FILE(1) -> The message is a file, incompatible with other flags.
* 
* FLG_REQ(2) -> The message is a request to the server or a response from the server.
* 
* FLG_EMPTY(4) -> The body of the message is empty 
* (the length is at minimum 1, a single \0, or rather a truncated message)
*
* FLG_LIST(8) -> The message contains a list of active names, if active together with FLG_REQ, 
* it means the sender requests the list of active clients
*
* FLG_DEST_CONN(16) -> Reachability of the destination from the server,
* only used for handling a prod request (on if reachable, off if not).
*
* FLG_INV_REQ(32) -> The flag is toggled server-side in the case a client's request is invalid
* 
* FLG_NO_TOKEN(64) -> This flag signals that the recipient doesn't gain the right to 
* send a message after receiving the message with this flag.
*/
enum msg_flags
{
	FLG_FILE = 0x1U,
	FLG_REQ = 0x1U << 1,
	FLG_EMPTY = 0x1U << 2,
	FLG_LIST = 0x1U << 3,
	FLG_DEST_CONN = 0x1U << 4,
	FLG_INV_REQ = 0x1U << 5,
	FLG_NO_TOKEN = 0x1U << 6
};


/*
* Initializes the WinSock2 library, with the requested version 2.2.
* 
* Return value: Returns 0 on success, and a non-zero value on failure.
*/
int32_t ws2_init();


/*
* Attempts to send an amount of data specified by the len parameter over the
* socket sock, from the buffer buf.
* 
* Parameters:
* @sock [in] -> A handle to an open and connected socket.
* 
* @buf [in] -> Pointer to the buffer to be read from.
* 
* @len [in] -> The amount of data to be sent.
* 
* Return value: On success, the function returns the number of bytes written,
* on failure, the function returns the negated value of the WinSock2 socket error code
* that caused the failure.
*/
int32_t send_all(SOCKET sock, const void* buf, int32_t len);


/*
* Attempts to read an amount of data specified by the len parameter from the socket sock,
* to the buffer buf.
* 
* Parameters:
* @sock [in] -> A handle to an open and connected socket.
* 
* @buf [out] -> Pointer to the buffer to be written to. (note: the buffer must be able to accomodate
* the amount of data specified by len)
* 
* @len [in] -> The amount of data to be received.
* 
* Return value: On success, the function returns the number of bytes written,
* on failure, the function returns the negated value of the WinSock2 socket error code
* that caused the failure.
*/
int32_t recv_all(SOCKET sock, void* buf, int32_t len);


/*
* Checks whether the given file name containts a .txt extension.
* 
* Parameters:
* @filename [in] -> Pointer to the buffer or string literal containing
* the file name to be examined.
* 
* Return value:
* If the given file name contains a .txt extension, the return value is 1,
* otherwise it is 0.
*/
uint8_t is_txtfile(const char* filename);


/*
* Prepares the header struct for sending in accordance with the provided parameters.
* 
* Parameters:
* @hdr [out] -> Pointer to a header struct.
* 
* @msgtype [in] -> A bitmask containing the relevant header flags.
* 
* @sender [in] -> Pointer to a null-terminated string representing the name of the sender.
* 
* @recipient [in] -> Pointer to a null-terminated string representing the name of the recipient.
* 
* @file_name [optional, in] -> Pointer to a null-terminated string representing the name of the file being sent.
* 
* @size [in] -> Total size of the body of the message being sent.
*/
void prepare_header(header* hdr, uint8_t msgtype, char* sender, char* recipient, char* file_name, uint32_t size);


/*
* Makes the provided socket a blocking one.
* 
* Parameters:
* @sock [in] -> Handle to an open socket to be made blocking.
*/
void setblocking(SOCKET sock);


/*
* Attempts to grab the header of a message from the given socket.
* 
* Parameters:
* @snd [in] -> Handle to an open and connected socket to read the header from.
* 
* @m_header [out] -> Pointer to a header struct to be filled out.
* 
* Return value:
* On success (reading the full header) the function returns the
* number of bytes read. On failure, the function returns the negated
* value of the WinSock2 error code that caused it to fail.
*/
int32_t get_header(SOCKET snd, header* m_header);


/*
* Flushes the remaining contents of stdin up to the
* first newline or EOF character. 
* Warning: if there is no data present in stdin, 
* the function may block.
*/
void flush_stdin();


/*
* Creates a chunk header based on the parameters provided.
* 
* Parameters:
* @flags [in] -> Bitmask representing the chunk header flags to be set.
* @size [in] -> Size of the chunk.
* 
* Return value:
* The function returns the created chunk header.
*/
ch_header create_ch_hdr(uint8_t flags, uint16_t size);


/*
* Flushes all the currently present data to be read in
* the socket's internal buffer.
* 
* Parameters:
* @sock -> Handle to an open and connected socket.
*/
void flush_sock_buf(SOCKET sock);


/*
* Removes the absolute or relative path from the file name.
* If provided file name does not contain a path, the function
* does nothing.
* 
* Parameters:
* @filename [in, out] -> Pointer to the buffer containing the file name.
*/
void rmpath_filename(char* filename);


/*
* Checks whether the file with the given name exists
* on the given absolute or relative path.
* 
* Parameters:
* @filename [in] -> Pointer to the string containing the file name.
* 
* Return value:
* If the file exists, the function will return 1.
* Otherwise, it will return 0.
*/
uint8_t file_exists(const char* filename);


/*
* Sets the send timeout on a blocking socket to send, and recv timeout to recv.
* 
* Parameters:
* 
* @sock [in] -> Socket to set the timeouts for.
* 
* @send [in] -> send timeout
* 
* @recv [in] -> recv timeout
* 
* Return value: 0 if successful, negated WinSock2 error code that caused the function to fail if unsuccessful.
*/
int32_t set_sock_timeout(SOCKET sock, uint32_t send, uint32_t recv);


/*
* Converts a common WinSock2 error code to a pre-defined string literal.
* 
* Parameters:
* 
* @wserr [in] -> WinSock2 error code to be converted.
* 
* Return value: String literal corresponding to the error code if it's a common error code, otherwise the string literal
* "Uncommon error code".
*/
char* common_wserrno_tostr(int32_t wserr);