/*
* *****************************************************************************
* 
* Client handling functions and specifically client-tied utilities and structs. 
*
* *****************************************************************************
*/

#pragma once
#include <WinSock2.h>
#define HT_SIZE_CONST 32
#define HCONST 137

typedef struct status_context status_context;

typedef struct header header;


/*
* Struct containing all data relevant to a client.
* 
* @clname -> buffer containing the username of the client
* 
* @clsocket -> the client's socket
* 
* @timedout -> flag/boolean value signalling if the client has overstepped
* their time to send a message
* 
* @disconnected -> flag signalling the client has disconnected
* 
* @ordinal_num -> the ordinal number of the client, assigned upon connection,
* represents the ordinal number of the client's connection, effectively an internal
* UID, only used for sorting purposes.
* 
* @next -> pointer to the next client in the linked list, used only in case of a
* collision in the hash table with a given slot.
*/
typedef struct client
{
	char clname[32];
	SOCKET clsocket;
	uint8_t timedout;
	uint8_t disconnected;
	uint8_t ordinal_num;
	struct client* next;
}client;


/*
* Wrapper struct containing all data relevant to client handling functions.
* 
* @ptr_htable -> hashtable containing all clients (for usernamed-based access)
* 
* @ptr_cli_arr -> array containing all the clients (for ordered access)
* 
* @ptr_client_num -> pointer to the total number of clients (initially said to be connected)
* 
* @ptr_client_cnt -> pointer to the number of currently connected clients
* 
* @ptr_htsize -> pointer to the total size of the hashtable (client_num * HTSIZE_CONST)
*/
typedef struct cl_param_struct
{
	client** const ptr_htable;
	client** const ptr_cli_arr;
	uint8_t* const ptr_client_num;
	uint8_t* const ptr_client_cnt;
	uint32_t* const ptr_htsize;
}cl_param_struct;


//Macro to unpack the client parameter struct.
#define UNPACK_CL_PARAMS(clparams) \
		uint8_t client_num = *clparams->ptr_client_num; \
		uint8_t* client_cnt = clparams->ptr_client_cnt; \
		client** clients_arr = clparams->ptr_cli_arr; \
		client** clients_ht = clparams->ptr_htable; \
		uint32_t htsize = *clparams->ptr_htsize;


/*
* Hashes the provided name and returns the hash value.
* 
* Parameters:
* 
* @name [in] -> name to be hashed
* 
* Return value: The resulting hash.
*/
uint64_t hash_name(const char* name);


/*
* Fetches a client from the hashtable based on the provided username.
* 
* Parameters:
* 
* @htable [in] -> Hashtable with the clients.
* 
* @name [in] -> username of the client to be fetched.
* 
* @htsize [in] -> total size of the hashtable.
* 
* Return value: Pointer to the client struct of the requested client if
* the client exists, otherwise NULL.
*/
client* s_get_client(client** htable, const char* name, uint32_t htsize);


/*
* Sets a client's disconnected flag to 1, and sets global flag g_someone_hasdisconnected
* to 1.
* 
* Parameters:
* 
* @cl [in, out] -> Pointer to the client to be marked as disconnected.
*/
void s_set_disconnected(client* cl);


/*
* Creates a client with the socket clsocket, username clname, inserts it into the hashtable, and a reference (pointer) to it
* in the client array. Increments client_cnt if successful.
* 
* Parameters:
* 
* @htable [in, out] -> hashtable for the client to be added to
* 
* @htsize [in] -> total size of the hashable (number of total slots)
* 
* @clsocket [in] -> a valid socket connected to the client
* 
* @clname [in] -> the client's username
* 
* @clients_arr [in, out] -> the client array (or rather client reference array)
* 
* @client_cnt [in, out] -> pointer to the client counter, incremented with each successful addition
*/
void s_ht_addclient(client** htable, uint32_t htsize, SOCKET clsocket, char* clname, client** clients_arr, uint8_t* client_cnt);


/*
* Accepts a client and receives the client's initial message containing their username, blocks for up to ACCEPTWAITTIMEOUT many milliseconds
* waiting for the connection, otherwise returns INVALID_SOCKET if a connection isn't accepted within the time-out period.
* 
* Parameters:
* 
* @sock [in] -> a valid, listening socket
* 
* @recvtimeout [in] -> receive timeout to be set on the accepted socket
* 
* @sndtimeout [in] -> send timeout to be set on the accepted socket
* 
* @buf [out] -> buffer for the username of the client to be received.
* 
* Return value: The accepted socket of the client, INVALID_SOCKET if no connection is accepted.
*/
SOCKET s_accpt_and_recv_init_msg(SOCKET sock, uint32_t* recvtimeout, uint32_t* sndtimeout, char* buf);


/*
* Selects the first available client (one that is neither timed out nor disconnected) in the same sequential order they connected.
* Every examined client is also tested ad-hoc and are marked as disconnected if connection to them is broken. The function is "daisy-chained",
* meaning that if the provided status context struct's status is anything other than OK, then the function returns NULL immediately without modifying
* the status context struct or doing anything else. Otherwise, if the status is OK, but no suitable client can be found, the status is set to NONE_AVAIL.
* 
* Parameters:
* 
* @clparams [in, out] -> Pointer to the struct containing all the relevant client-handling parameters.
* 
* @scon [in, out] -> Pointer to the status context struct to be examined, and modified if necessary
* 
* @events [in, out] -> Pointer to the events/flags bitmask that serves as the criteria for checking availability, modified with the return events/flags.
* 
* Return value: Pointer to the client struct belonging to the first available client, or NULL if no suitable client could be found.
*/
client* s_select_first_avail(cl_param_struct* clparams, status_context* scon, uint16_t* events);


/*
* Accepts up to N-many (client_num) connections and adds the clients to their relevant data structures, updating client_cnt to the total number of connected
* clients, stop receiving new connections either when client_cnt reaches client_num or accept wait timeout between 2 accepts is reached.
* 
* Parameters:
* 
* @ssock [in] -> a valid, listening socket
* 
* @clparams [in, out] -> pointer to the struct containing all the relevant parameters for client-handling
* 
* @recvtimeout [in] -> receive timeout to be set on the accepted client sockets
* 
* @sndtimeo [in] -> send timeout to be set on the accepted client sockets
* 
* @recvbuf [in, out] -> buffer to receive the usernames
*/
void s_accept_and_add_all(SOCKET ssock, cl_param_struct* clparams, uint32_t* recvtimeout, uint32_t* sndtimeo, char* recvbuf);


/*
* Sends a header (with the sender name set to "server", followed by a welcoem message to the client socket specified by sock. 
* The function is daisy chained, it will return immediately if the status field in the status context struct is not OK, 
* preserving the original error status. In case the function call fails for another reason, it will return a modified 
* version of the status context struct.
* 
* Parameters:
* 
* @hdr [in] -> Pointer to he header to be sent.
* 
* @sock [in] -> A valid, connected client socket.
* 
* @buf [in] -> Buffer containing the welcome message.
* 
* @scon [in] -> Status context struct to be examined and possibly modified.
* 
* Return value: A status context struct, modified or unmodified, depending on whether the function has
* returned early, if it was successful or if an error has occured in the function. If the status context's
* status field was set to something other than OK before the function was called, the function will return
* immediately without doing anything and it will return the unmodified struct, if the call is successful it will
* return a status context with the unmodified status field, but possibly modified other fields. If the function call fails
* it will set the status field in the status context struct to the corresponding error, also setting the error field if the
* error status was caused by a socket error.
*/
status_context s_send_welcome(const header* hdr, SOCKET sock, const void* buf, status_context scon);


/*
* Formats a welcome message containing the name provided.
* 
* Parameters:
* 
* @buf [out] -> Buffer to write the welcome message to.
* 
* @name [in] -> The name of the recipient.
* 
* @m_header [out] -> Pointer to the header struct to be modified.
*/
void s_prep_welcome(char* buf, char* name, header* m_header);


/*
* Removes the client with the username <name> from the hashtable, freeing their slot and setting it to NULL, 
* as well as removing the reference from the client array. Doesn't update client_cnt, nor does it re-order the
* client array.
* 
* @htable [in, out] -> Pointer to the clients hash table.
* 
* @cli_arr [in, out] -> Pointer to the client reference array.
* 
* @name [in] -> Name of the client to be removed.
* 
* @htsize [in] -> The total size of the hashtable.
* 
* @client_cnt [in] -> The current client count (caller is responsible for ensuring it is correct).
* 
* Return value: AOK(0) if successful, otherwise BAD_CALL(15)
* 
*/
uint8_t s_remove_client(client** htable, client** cli_arr, char* name, uint32_t htsize, uint8_t* client_cnt);


/*
* Checks whether the g_someone_hasdisconnected flag has been set, returns early if it hasn't, otherwise it resets the flag
* and iterates through the client reference array checking each client's disconnected flag, removes those with the toggled
* disconnected flag (from the hashtable, and their reference from the array). Afterwards properly decrements the client counter
* and sorts the reference array according to the clients' ordinal number if any clients have been removed.
* Note: Does not resize the array, merely removes references to disconnected clients and moves those "slots" to the end of the array.
* 
* Parameters:
* 
* @clparams [in, out] -> Pointer to the clparams struct containing all the relevant client-handling parameters.
*/
void s_remove_disconnected(cl_param_struct* clparams);