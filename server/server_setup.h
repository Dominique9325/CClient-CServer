/*
* **********************************************************************************************
* 
* Server setup and cleanup. Creating a socket, binding it to an interface and port, listening,
* end-of-operation cleanup before the program terminates.
* 
* **********************************************************************************************
*/

#pragma once
#include <WinSock2.h>

typedef struct client client;

#define MAX_LISTEN_QUEUELEN 256


/*
* Sets s_interface to the network interface based on the option number provided, the interface
* is a regular UTF-8 string of an IPv4 address in dotted decimal notation. The options include:
* 1 - localhost/loopback (127.0.0.1)
* 2 - the main active network adapter of the current device
* 3 - manually entering the address of the interface
* 4 - all interfaces (0.0.0.0)
*
* Note that any unsupported option number will revert to the default case of localhost/loopback interface.
* 
* Parameters:
* 
* @s_interface [out] -> Buffer to accomodate the interface address in dotted decimal notation, must be at least
* 16 bytes big, though it's recommended that it be INET_ADDRSTRLEN bytes big, which may differ based on the environment
* used. For example on Windows, with WinSock2, it's defined as 22 bytes.
* 
* @s_interface_optnum [in] -> Option number for selecting the interface as described earlier, invalid option numbers will just
* select the default interface.
*/
void s_set_interface(char* s_interface, int8_t s_interface_optnum);


/*
* Binds the socket to the provided interface and port and listens.
* 
* Parameters:
* 
* @s_interface [in] -> Buffer holding the interface address in dot-decimal notation.
* 
* @sock [in] -> A valid socket handle.
* 
* @port [in] -> Port to listen on.
*/
void s_bind_listen(char* s_interface, SOCKET sock, uint16_t port);


/*
* Sequentially cleans up the client-related data structures (Client hashtable and reference array), setting the pointers
* to NULL. To be used when the 2 client-handling related data structures are no longer needed.
* 
* Parameters:
* 
* @htable [in, out] -> Triple pointer to the hashtable to be cleaned up.
* 
* @cli_arr [in, out] -> Triple pointer to the client reference array to be cleaned up.
* 
* @client_num [in] -> Total number of clients originally expected representing the length of the client reference array
* and the multiplier for the total size of the hashtable.
*/
void s_cleanup(client*** htable, client*** cli_arr, uint8_t client_num);