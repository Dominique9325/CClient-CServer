# CClient-CServer
Messaging client and server app(s) written entirely in C, using only the Microsoft C runtime libraries (Microsoft's implementation of the C standard library), and the Windows Sockets API v2.2 (WinSock2).


## Table of contents
[What it entails](#what-it-entails)

[Technologies used](#technologies-used)

[Instructions for use](#instructions-for-use)

[Detailed principle of operation](#detailed-principle-of-operation)

[Server](#server)

[Client](#client)

[Possible improvements and conclusion](#possible-improvements-and-conclusion)
  


## What it entails?
The project consists of a server applet and a client applet, the server supports up to 256 clients in theory (though in practice it is likely far less, it was infeasible and even impossible for me to test it with such a large number of clients). The client applet contains a mini-lexer, for tokenizing and interpreting input, converting it into meaningful, custom-defined commands (and arguments for those commands). The commands supported include sending a message to other connected clients (done via username), sending a file to other connected clients, sending requests directly to the server (requesting a list of active clients and prodding any specific client for connectivity), a disconnect command and a help command, containing information about all the currently implemented commands. The server, as mentioned, supports routing messages and files (up to 4 GB big) between clients, processing direct requests, and it has custom error handling mechanisms for handling several different common types of errors. Both applets are single-threaded so that imposes some limitations which will be discussed later on.


## Technologies used
As mentioned, no external libraries were used, the only libraries used were the ones provided by the Microsoft C Runtime, as well as the Windows Sockets API, provided by the Microsoft Windows SDK. All error handling, input handling, routing and utility functions were written by me. The build system used was MSBuild provided by Microsoft Visual Studio 2022 Community Edition. I wrote a small powershell script that starts multiple instances of the client applet at once in separate powershell windows as well as (optionally) an instance of the server applet, both the debug build and the release build. This was mainly used to facilitate debugging and to cut down on manual repetition of manually opening multiple powershell windows and starting the applet in each one of them. The debug builds of the applets also differ from the release builds because any timeouts are removed, a lot of user input is cut back in favor of default values being used, once again, to reduce repetition when debugging.

## Instructions for use
Since this project relies on a Windows-specific API, as well as some Microsoft-defined functions which are not part of the C standard, this project can only be built and executed on Microsoft Windows, any version that supports WinSock v2.2 and up, no external dependencies are needed. However, since Windows does not provide a C/C++ toolchain by default, and they are trickier to install and use than on Linux, and the fact the project uses Microsoft's proprietary build system. 

If one wishes to build the project manually, it is recommended to clone the repository (git clone https://github.com/Dominique9325/CClient-CServer/), and open the .sln (solution) file in Visual Studio and clicking build solution (hotkey CTRL + Shift + B by default). At this point you may either launch the resulting executable directly through visual studio, by double clicking on it in Windows File Explorer, or by launching it as a command in PowerShell/Command Prompt. In the latter case it's advised to add the path to the folder containing the executables to the PATH environment variable for easier access. For those who don't have Visual Studio downloaded and don't wish to download it, I also included the end-result release build executables for the server and the client in the repository. 

In the debug build the server is configured to expect 3 clients by default, the default interface to listen on is localhost/loopback (127.0.0.1) and the default port 5234, in the release build you need to enter all these manually. As for the client, in the debug build the default IP address and port to connect to are set to the same ones as the server is listening on, the default folder for receiving files is D:\testfolder\, though note that it will not work unless the folder at that path already exists, it can either be manually changed to something else or one can use the release build where it can be set upon starting the applet. The only thing that need be enter by the user in the debug build for the client is the username, while in the release build everything previously mentioned needs to be entered manually. 

I've tested the applets between 2 devices within the same network (though one is a virtual device - WSL2), and it works as expected, though the latency and packet loss are obviously minimal, if not non-existent in small local network, therefore the performance is scarcely different from just testing it on the loopback interface. In theory, this should work between two devices on separate networks (over the internet), though in that case port-forwarding would need to be set up for the port listened on by the server, also it is not advised from a security standpoint because the communication is unencrypted, and application itself is not secure enough to withstand attacks from third parties, so it is best tested in a local, controlled environment. For a list of commands supported by the client applet and how to use them, type /help.


## Detailed principle of operation

### Server

#### General operation
The server is first started, the user selects a network interface and port to listen on and specifies the expected number of clients that will connect, then the period for awaiting connections start. 

The server will wait for up to as many clients as specified to connect, or until 10 seconds have passed since the last connected client (whichever comes first), then comes the phase of selecting the initial sender, the server sequentially polls each client's socket (in the order that they connected), and if it's determined that the socket is writable, it attempts to send the welcome message to said client (it iterates through the clients until it reaches the first one whom it can successfuly send a message to). The client who receives the welcome message is the initial sender, it is now their turn to send a message. 

They now have 20 seconds (by default) to send a message to any other client or send a request to the server, in a sense, they have a pseudo-MAC token (Media Access Control). If the time window expires before the message is sent, the MAC token is relocated to the first available client and the current client is marked as timed out (in case the first suitable client is seeked out again, they will not be available as such), though this flag is reset when someone else sends a message to the client. 

Only one client can send a message at any given moment, whereas all other ones are infinitely waiting to receive a message (or lose connection to the server). The MAC token is implicit, so it does not exist as a physical object but the server knows who has the MAC token at any given moment, messages sent by timed out clients are flushed by the server and not forwarded. 

The way this is implemented is a consequence of there not being a convention solution for non-blocking user input on Windows other than creating a separate thread, which I wanted to avoid doing since the entire project
was already inherently single-threaded, and using a separate thread for handling input would leave the question - Why didn't I just make everything multi-threaded, why didn't I just ditch the MAC token logic? 

So, one thing I should clarify first before proceeding, each message is preceded by a message header, which details what type of message it is (file, regular message or server request), who sent it and to whom and the size of the entire message (minus the already received header, which has a fixed size). So the server first receives the header from the designated sender (the client who currently holds the MAC token), then the header is interpreted and the corresponding action taken. 

In case of regular text messages, the message body is received from the sender, then both the header and the message body are forwarded to the recipient. In the case of files, there is an intricate "protocol" for transferring files. Since the files supported can be up to 4 GB big, they're too large to fit into a buffer in one go so files are sent in 64 KB chunks and a new type of header is introduced - the chunk header (contains a flag bitmask and the size of the incoming chunk). First the server checks whether the recipient is available, then it forwards the header to the recipient, after which it sends a control chunk header to the sender, indicating that everything is OK and that they can send a chunk, at which point a chunk header and the amount of data specified by the chunk header are expected by the server, when those are received they are both forwarded to the recipient. 

There is a control chunk header sent to the sender before they send each chunk. As for server requests, there are 2 different requests, a request for a list of active names, and a connectivity/prod request, they are both processed by the server and a response is sent back to the sender.

#### Error handling
The way the main event loop is set up is that most, if not all functions in the main event loop are "daisy-chained". They all rely on a "status context" struct, containing information about the context of the latest caller the struct was passed to. The struct serves to isolate the cause of error/failure of processing a certain request, when one of those "daisy-chained" functions fails, it returns early. 

Any subsequent call to any of the daisy-chained functions with the same status context struct passed to it will yield an immediate return, leaving the status context struct unmodified, containing information about the context where the failure originally occured. The only function that isn't daisy chained is the error handling function. Depending on the status field of the status context struct, it will call an appropriate handler. 

For example, if a client disconnects in the middle of sending a message, the server will only partially receive the message, so said failure is called a partial receive error, and the procedure for handling it is notifying the would-be recipient and giving them the MAC token, also, in case the message was a file, abolishing the file transfer on the recipient side correctly. I will not go into detail about each type of error and the procedure for handling it, but I will mention some of the possible errors. 

Other than partial receive, there is also a partial send, a partial header receive, a malformed header error, a malfored request error, message wait time out error, connection closed error (suddenly or gracefully, sender or recipient), unknown/unresolved client name error, etc. The function also handles recurring errors. For example, if the recipient you were to notify about the sender's disconnection disconnected themselves before they received the full message, then that would count as an "additional error", and an entirely new suitable candidate for holding the MAC token will be selected. If no such suitable client is found, then the server operation is ceased, and the server gracefully shuts down. 

Clients who are "disconnected" aren't removed on the spot, they're marked as disconnected, and a function for cleanup of disconnected clients that runs once per iteration cleans up said clients, the function returns immediately if no people have disconnected in the iteration (signalled by a static global flag, set by the function that marks clients as disconnected, reset by the cleanup function). It's also important to note that there are 2 different types of statuses, low-level and high-level ones. The low-level ones are generated by an error happening in a low-level function, and if said low-level function call that resulted in an error was from a high-level function, the status is automatically converted to a high-level one based on the context (for example s_recv_all causing an error, called from s_get_header, the high level status here would be partial header receive).

#### Data structures and algorithms used
Client handling is done via 2 main data structures, a hash-table, which allows fetching a client by the username, and an array of "references" (pointers) to the same client structs, which allows for ordered, sequential access, unlike the hash-table which only supports random access. 

The client itself is represented by a struct containing their socket handle, username, timed out flag, disconnected flag, and a unique ordinal number (order in which they connected). They are inserted into the hashtable by the value of the hash derived from their username. The hashing function used is primitive and manually implemented, it's a type of polynomial rolling hash, not secure for cryptographic purposes but good enough for ensuring an acceptably even distribution, resulting in a relatively low number collisions in this particular use case. 

The collisions in the hash table are resolved via linked lists, each slot in the hash-table represents the head node of a singly-linked list. The array contains pointers to the client structs contained in the hash table, allowing for ordered, sequential access, it's ordered by the ordinal number of each client instance. 

As mentioned, clients are initially just marked as disconnected, and the function which runs once per iteration cleans up said clients, which is more efficient. A client is actually removed by correctly removing them from the linked list, if they were in the head and the only client in that slot, freeing the entire slot and setting it to NULL, and the reference is removed from the array.

The function that cleans up disconnected clients first checks the static global flag to see if anyone has disconnected, then resets that flag and iterates through the reference array, checking each client's disconnected flag, removing the disconnected ones from the hashtable via their username. At the end of the cleanup function, the array is sorted using quicksort, maintaining the aforementioned order, placing removed references at the end of the array and decrementing the client counter by the number of the clients who have been cleaned up, thus the array is never actually physically resized, it's just logically resized, the hashtable itself is also never resized because it would require re-computing the hash value for each client every time a client is removed.

Just before the server is shut down the complete cleanup function is called, which just iterates through the array and frees every single remaining client.

### Client

#### General operation
The client types the IP address and port of the sever to connect to, the path to the folder to use for receiving incoming files and their desired username (note: conflicting usernames are not allowed, if someone other client on the server already has the same username, the person connecting with said username is immediately disconnected). 

Upon successfully connecting to the server, the client awaits a message from anyone, whether that be the server (welcome message) or any other client sending them a message, at which point they will be the ones to receive the MAC token and will be able to send a message to someone else (or a server request directly to the server). 

Since remote, indirect peer disconnections are handled gracefully by the server, any connection errors the client encounters, whether they be on the host or peer side, are treated as fatal and operation ceases, this allows for a lot simpler, albeit probably less robust error handling. Since the server is the client's only connection point to any other client, losing connection to it is naturally considered fatal and unrecoverable. Errors unrelated to the networking part of the client's operation are treated as non-fatal (i.e. trying to open a non-existent file for reading) and are handled gracefully. 

As previously mentioned the client has multiple supported commands, including /list, /message, /file, /prod, /disconnect and /help. /help, /disconnect and /list (if not used with the renew parameter) are local commands, meaning they do not send any outgoing data over the socket, meaning the they can be used as many times as wanted without losing the MAC token, but they do not reset the message wait time-out. It is, however, possible to send a message to yourself, and this does reset the message wait timeout period, while allowing you to keep the MAC token, though it is not possible to send a file to oneself due to the underlying mechanism of file transfer, it will fail and produce an error. 

The file transfer mechanism works exactly as described in the server's principle of operation. Any file transferred is chunked, when sending a file, a control chunk header is sent by the server before sending each chunk header and chunk. Data is first read from an open file handle into a buffer, then a chunk header is generated and sent, followed by the chunk body itself. On the receiving side, a chunk header is received before each chunk, data is received into a buffer, and then copied from a buffer into a file open for writing. There is a mechanism that protects against file naming conflicts, that alters the name of the received file in case a file with the same name already exists at the designated path.

#### Data structures and algorithms used
Obviously, the header and chunk header structs are used by both the server and the client, in order to ensure a cosistent memory layout and size, no matter the compiler, the struct is packed, meaning that no padding bytes are added by the compiler, which is relevant because the two structs are meant to be sent over a network. 

The input interpretation is done in an interesting manner. After the user types a command, the buffer containing the input is passed to a mini-lexer, which recursively breaks down the input string into tokens via 2-level recursion. First the buffer is tokenized by the delimiter ' " ' - double quotation marks, then every resulting token at an even recursive depth (every even token) is further subtokenized by the delimiter ' ' - whitespace. In effect, this means that anything inside double quotation marks is not further sub-tokenized and counts as a single token, as is the case in most CLI interpreters, meanwhile anything outside them is broken down further into separate tokens. 

Due to the nature of the way recursion works (adding tokens in reverse order), and the unpredicatbility of the end-number of tokens, tokens are added added as nodes to a linked list that has a dummy head, since they're added in the front the end result is a linked list of tokens in correct order.

The function which creates an individual token behaves in a manner similar to the function strtok, It searches for the delimiter, then replaces it with a null-terminator, effectively making a separate string, and returns a pointer to the beginning of the token, except neither strtok, nor strtok_s (Microsoft's implementation of strtok_r) suited me, so I created my own version.

Both of those aforementioned functions require you to set up an "initial state" before being able to iteratively call the function, which is obviously not suitable for a recursion. Strtok uses an internal static variable which keeps track of how far the buffer has been tokenized, whereas strtok_s expects you to pass it a "context" pointer, pointing to just after the latest token created. Instead of doing either of that, I pass a pointer to the pointer to the buffer to my function, my function creates a token, returns a pointer to the beginning of the token, and offsets the provided pointer to just behind said token. In effect, just like the other functions, it overruns the buffer, but it also offsets the pointer given to it, therefore a copy of the pointer to the original buffer should be provided. 

Anyways, the first token is then enumerated, and the corresponding command opcode returned. Then, depending on the command, the other tokens are either evaluated or ignored (depending on the number of arguments the command accepts, some commands accept a variable number of arguments). Then, based off the command and the arguments, a header is generated and an action function pointer set.


## Possible improvements and conclusion
As mentioned, the applets are single-threaded, the client's blocking input provides a huge obstacle that regard, I've considered using the Windows API for improvising non-blocking input, but there simply wasn't a single satisfactory way to achieve it. The issue with console objects in Windows API is that they're event-driven, and not stream driven unlike on Unix-based systems. The buffer is not a buffer of the characters written to stdin, and stdin is not a file descriptor on windows, it's a standard input handle, which could be just about anything, by default it's a console object. 

The issue with console objects on Windows, is that they don't process input, they process events - events such as resizing the console window, clicking on it, ANY key press, etc. I tried some workarounds by creating events and signalling them, etc. but none of those yielded satisfactory results. In the end, what I would need, is a separate thread for input, but using that would require me to redesign my entire project. 

Of course, moving to a multi-threaded design would be highly beneficial, it would drastically improve the usability of the two applets and wouldn't require me to implement my own MAC token logic and having only one person be able to send a message at any given moment. In addition, if I were to use an SSL library (i.e. OpenSSL) and wrap the sockets in an SSL layer, communication would be end-to-end encrypted and, with further improvements, the project would potentially be safe enough to deploy for use over the internet rather than just a local network. 

Nevertheless, I would say this project has served its purpose, I have learned a lot about sockets and network programming, I have deepened my knowledge of C, and of data structures and algorithms. I've also learned how to read and write documentation, as well as how Windows APIs function to an extent.
