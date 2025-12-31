#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "input_parsing.h"
#include "shared_utils.h"
#include "msg_handling.h"
#include "client_utils.h"

//Struct representing a single command. Contains the command name, the command operation code and the manual for the command.
typedef struct _command
{
	const char* cmdname;
	const uint8_t opcode;
	const char* manpage;
}_command;


//All currently supported commands.
static const _command _cmds[] = 
{   
	{
		"list", 0x1U, "/list <?action> - Prints out the list of all active users, currently"
						"the only supported action is renew. If action is not specified, and "
						"a local list of active users exists, the local copy will be printed out, "
						"if the renew action is specified, it will request a new list from the server."
	},		  
	{
		"prod", 0x2U, "/prod <username> - Checks whether the user specified by the username parameter"
						"is reachable by prompting the server."
	},
	{
		"message", 0x3U, "/message <username> <body> - Sends a text message with the specified body to"
							"the selected user."
	}, 
	{
		"file", 0x4U, "/file <username> <file path> - Attempts to send a file at the specified to the"
						"specified user if the file exists and can be opened for reading."
	}, 
	{
		"disconnect", 0x5U, "/disconnect - Disconnects from the server."
	}, 
	{
		"help", 0x6U, "/help - Provides a list of all existing commands and how to use them."
	} 
};

static const uint8_t _ncmds = (uint8_t)(sizeof(_cmds) / sizeof(_cmds[0]));


//Converts the provided command string to the command opcode.
static uint8_t _fetch_command(const char* tok)
{
	if (!tok)
		return 0x0U;

	tok++;

	for (uint8_t i = 0; i < _ncmds; i++)
	{
		if (!strcmp(tok, _cmds[i].cmdname))
			return _cmds[i].opcode;
	}

	return 0x0U;
}

//Nodes which make up a linked list of input tokens.
typedef struct _tkn_node
{
	char* token;
	struct _tkn_node* next;
}_tkn_node;


//Fetches and "separates" next token based on the delimiter, if such a token does not exist, it returns NULL.
//Note: "destroys" the provided buffer and increments the pointer provided.
static char* _getnextok(char** buf, char delim)
{
	if (!buf || !(*buf) || **buf == '\0')
		return NULL;

	uint64_t j = 0;
	while (**buf == delim)
		(*buf)++;

	char* tok = *buf;
	uint64_t i = 0;
	for (; (*buf)[i] != '\0' && (*buf)[i] != delim; i++);
	if ((*buf)[i] == '\0')
	{
		*buf = NULL;
		return tok;
	}

	(*buf)[i] = '\0';
	*buf += i + 1;

	return tok;
}

//Creates an initial linked list of token nodes containing only the dummy head.
static _tkn_node* _create_tk_list()
{
	_tkn_node* head = (_tkn_node*)malloc(sizeof(_tkn_node));
	if (!head)
		return NULL;
	head->token = NULL;
	head->next = NULL;
	return head;
}

//Creates a token node from the provided token and adds it to the list.
static void _add_tk_node(_tkn_node* list, const char* token)
{
	if (!list || !token)
		return;

	_tkn_node* newnode = (_tkn_node*)malloc(sizeof(_tkn_node));
	if (!newnode)
		return;
	newnode->token = (char*)token;
	newnode->next = NULL;
	if (!list->next)
		list->next = newnode;
	else
	{
		newnode->next = list->next;
		list->next = newnode;
	}
}

//Frees the provided list and sets the pointer to NULL.
static void _free_list(_tkn_node** list)
{
	if (!list || !(*list))
		return;
	else if (!(*list)->next)
	{
		free(*list);
		*list = NULL;
		return;
	}

	_tkn_node* aux = (*list)->next;
	while (aux)
	{
		free(*list);
		*list = aux;
		aux = aux->next;
	}

	free(*list);
	*list = NULL;
}

//Tokenizes the buffer recursively, first based on the delimiter provided, then based on whitespaces.
//In effect, tokens "between" the two of the provided delimiter will not be subtokenized (by whitespaces).
//The output is a linked list containing pointers to the end-result tokens.
//Internal function used by _tokenize.
static void __tokenize(_tkn_node** tklist, char** buf, char delim, uint32_t depth)
{
	char* token;

	if (!(token = _getnextok(buf, delim)))
		return;

	__tokenize(tklist, buf, delim, depth + 1);
	if (!(depth % 2))
		__tokenize(tklist, &token, ' ', 1);
	if (!(*tklist))
		*tklist = _create_tk_list();

	if (token && strlen(token))
		_add_tk_node(*tklist, token);
}

//Tokenizes the input buffer based on whitespaces, whitespaces inside double quotation marks are ignored, i.e.
//anything inside double quotation marks is treated as a single token. Returns the linked list containing pointers to all
//the tokens, or NULL if no tokens exist. It overwrites the provided buffer in the process. Note that the linked list nodes
//contain pointers to the tokens which live inside the buffer, and do not contain buffers containing the tokens themselves, therefore
//overwriting buf before copying the tokens to a safe place will overwrite/corrupt them.
static _tkn_node* _tokenize(char* buf)
{
	_tkn_node* tklist = NULL;
	__tokenize(&tklist, &buf, '\"', 0);

	if (tklist && !tklist->next)
	{
		_free_list(&tklist);
		tklist = NULL;
	}

	return tklist;
}

u_clfunc_status interpret_input(header* hdr, char* buf, const active_names* anames, const char* uname, afptr* action)
{
	if (!buf || buf[0] != '/')
	{
		fprintf(stderr, "Error: Invalid input.\n");
		return ENONFATAL;
	}

	char* newline = strrchr(buf, '\n');
	if (newline)
		*newline = '\0';

	*action = NULL;
	_tkn_node* token_list = _tokenize(buf);
	if (!token_list || !token_list->next)
		return ENONFATAL;

	_tkn_node* arg_cursor = token_list->next;
	uint8_t command = _fetch_command(arg_cursor->token);
	_tkn_node* tmp = arg_cursor;
	int i = 0;

	uint8_t retval = OK;
	uint64_t size = -1;
	switch (command)
	{
		case 0x0U:
			fprintf(stderr, "Error: Unknown or poorly formatted command.\n");
			retval = ENONFATAL;
			break;

		case 0x1U:
			if (arg_cursor->next && !strcmp(arg_cursor->next->token, "renew"))
			{
				buf[0] = '\0';
				prepare_header(hdr, FLG_REQ | FLG_LIST | FLG_EMPTY, (char*)uname, "server", NULL, 1);
				*action = send_req;
				break;
			}
			else
				print_anames(anames);
			retval = REPEAT;
			break;

		case 0x2U:
			if (!arg_cursor->next)
			{
				fprintf(stderr, "Error: /prod: Invalid number of arguments.\n");
				retval = ENONFATAL;
				break;
			}
			prepare_header(hdr, FLG_REQ | FLG_DEST_CONN, (char*)uname, "server", NULL, (uint32_t)strlen(arg_cursor->next->token) + 1);
			strcpy(buf, arg_cursor->next->token);
			*action = send_req;
			break;

		case 0x3U:
			if (!arg_cursor->next)
			{
				fprintf(stderr, "Error: /message: Unspecified recipient.\n");
				retval = ENONFATAL;
				break;
			}
			else if (!arg_cursor->next->next)
			{
				fprintf(stderr, "Error: /message: Unspecified message body.\n");
				retval = ENONFATAL;
				break;
			}
			prepare_header(hdr, 0, (char*)uname, arg_cursor->next->token, NULL, (uint32_t)strlen(arg_cursor->next->next->token) + 1);
			strcpy(buf, arg_cursor->next->next->token);
			*action = send_msg;
			break;

		case 0x4U:
			if (!arg_cursor->next)
			{
				fprintf(stderr, "Error: /file: Unspecified recipient.\n");
				retval = ENONFATAL;
				break;
			}
			else if (!arg_cursor->next->next)
			{
				fprintf(stderr, "Error: /file: Unspecified file.\n");
				retval = ENONFATAL;
				break;
			}

			if (!file_exists(arg_cursor->next->next->token))
			{
				fprintf(stderr, "Error: /file: File couldn't be opened.\n");
				retval = ENONFATAL;
				break;
			}
			else if ((size = file_size(arg_cursor->next->next->token)) > UINT32_MAX || size < 0)
			{
				fprintf(stderr, "Error: /file: The file is too large.q\n");
				retval = ENONFATAL;
				break;
			}
			prepare_header(hdr, FLG_FILE, (char*)uname, arg_cursor->next->token, arg_cursor->next->next->token, (uint32_t)size);
			*action = send_file;
			break;

		case 0x5U:
			retval = SIGQUIT;
			break;

		case 0x6U:
			retval = REPEAT;
			printf("\n----------------------------------------\nHere is a brief overview of all commands, a ? prefix means the parameter is optional:\n\n");
			for (uint8_t i = 0; i < _ncmds; i++)
				printf("%s\n\n", _cmds[i].manpage);
			printf("----------------------------------------\n");
			break;
	}

	_free_list(&token_list);
	return retval;
}