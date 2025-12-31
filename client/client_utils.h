/*
* *********************************************************************************************************
* 
* Header with client utility functions and structs - those which are non-specific or are far
* too few in number to be put into their own header, so they're lumped in together. Meant mainly for use
* by the client due to their purpose.
* 
* *********************************************************************************************************
*/

#pragma once
#include <stdint.h>

typedef struct active_names active_names;

/*
* Universal client function status.
* Represents all possible status return values of client-specific functions. 
* OK - Self-explanatory.
* EFATAL - Fatal, unrecoverable error, usually indicating direct connectivity has been lost.
* ENONFATAL - Nonfatal error which can be ignored, usually either an input parsing error, or error with a request
*			  that does not indicate that the direct connection to the server has been lost.
* SIGQUIT - Exclusive to input parsing, indicates that the command to kill the client has been executed.
*/
typedef enum u_clfunc_status
{
	OK,
	EFATAL,
	ENONFATAL,
	SIGQUIT
}u_clfunc_status;

#define REPEAT ENONFATAL

/*
* Sets the designated folder for receiving incoming files to the path name provided.
* Note: The folder should already exist, otherwise the functions that rely on the 
* designated folder will not work.
* 
* Parameters:
* @pathname [in] -> Path name to the folder to be set as the designated folder for receiving incoming files.
*/
void set_file_folder(const char* pathname);

/*
* Copies nnames-many names from the buffer buf to the provided active names struct. If the pointer
* points to an already existing object, it is freed and replaced by a new one. The pointer to the
* struct may be NULL (in which case a new struct will be allocated), but the pointer to the pointer
* to an active names struct may not be NULL. Note: It is expected that the amount of data in the buffer
* is a multiple of UNAME_MAXSIZE (32).
* 
* Parameters:
* @anames [out] -> Pointer to the pointer to an active names struct.
* 
* @buf [in] -> Pointer to the buffer containing the names to be copied.
* 
* @nnames [in] -> The number of names to be copied.
*/
void copy_anames(active_names** anames, const char* buf, uint8_t nnames);


/*
* Prints the active names to stdout.
* 
* Parameters:
* @anames [in] -> Pointer to the active names struct containing the names to be printed out.
*/
void print_anames(const active_names* anames);


/*
* Frees the active names struct properly and sets the pointer to NULL.
* 
* Parameters:
* @anames [in, out] -> Pointer to the pointer to the active names struct to be freed.
*/
void release_anames(active_names** anames);

/*
* Displays the progress bar with the corresponding percentage based on the
* input parameter. Note: The bar is redrawn on each call, nothing should be written
* to stdout between calls due to risk of being overwritten.
* 
* Parameters:
* @p_progress [in] -> The corresponding percentage for the loading bar. Legal range is between 0 and 100.
*/
void displ_progress_bar(uint8_t p_progress);


/*
* Appends the provided number in parentheses to the given file name.
* The file name must valid according to Windows and FAT32/NTFS.
* Works both for files with and without an extension.
*
* Parameters:
* @filename [in,out] -> Pointer to the buffer containing the file name
* for the number in parentheses to be appended to. The buffer must contain
* sufficient additional space for the number in parentheses to be appended.
*
* @num [in] -> The number to be put in parentheses and appended.
*/
uint8_t app_num_to_filename(char* filename, int32_t num);


/*
* Calculates the size of the file at the provided path, if the file exists and
* can be opened for reading.
* 
* Parameters:
* @filename [in] -> Absolute or relative path to the file.
* 
* Return value:
* If successful, the function will return the size of the file in bytes, otherwise
* it will return -1.
*/
int64_t file_size(const char* filename);


/*
* Prepends the name of the folder which is designated for saving incoming files to
* the given file name.
* 
* Parameters:
* @filename [in, out] -> Pointer to a writable buffer containing the file name, which
* is large enough to accomodate both the path to the designated folder and the file name.
* The total length of the concatenated path should not be longer than MAX_PATH, which is 260
* characters, including the null-terminator.
* 
* Return value:
* If successful, the function returns 0, otherwise it returns 1.
*/
uint8_t prep_ffolder_name(char* filename);