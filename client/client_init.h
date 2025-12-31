/*
* *****************************************************************************
* 
* Client initialization and connection to the server.
* 
* *****************************************************************************
*/
#pragma once
#include <WinSock2.h>
#include <stdint.h>

/*
* Creates a socket, attempts to connect to the given host and perform a simple identification handshake.
* 
* Parameters:
* @addr [in] -> Pointer to a string containing an IPv4 address in dot-decimal notation.
* 
* @port [in] -> Port number in host byte order.
* 
* @username [in] -> Pointer to a string containing the desired username.
* 
* Return value: A connected socket handle if the entire process was successful, INVALID_SOCKET if the process
* failed at any point. Note: Connecting with a duplicate username may result in forced disconnection by the remote
* host.
*/
SOCKET connect_to_server(const char* addr, uint16_t port, const char* username);