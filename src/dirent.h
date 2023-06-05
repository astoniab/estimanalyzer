#ifndef DIRENT_INCLUDED
#define DIRENT_INCLUDED

/*

    Declaration of POSIX directory browsing functions and types for Win32.

    Author:  Kevlin Henney (kevlin@acm.org, kevlin@curbralan.com)
    History: Created March 1997. Updated June 2003.
    Rights:  See end of file.
    
*/

#include <io.h> /* _findfirst and _findnext set errno iff they return -1 */

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct DIR DIR;

struct dirent
{
    char *d_name;
};

struct DIR
{
    //long                   handle; /* -1 for failed rewind */
	intptr_t               handle; /* -1 for failed rewind - changed to intptr_t to comply with handle data type in _findfirst64,_findnext64 - https://social.msdn.microsoft.com/Forums/SqlServer/en-US/2e7dc3cb-c448-43e7-9587-01abcc532d55/findnext-api-crashes-for-x64bit-application-in-windows-81-and-windows-server-2012?forum=vcgeneral */
    struct _finddatai64_t  info;
    struct dirent          result; /* d_name null iff first time */
    char                   *name;  /* null-terminated char string */
};

DIR           *opendir(const char *);
int           closedir(DIR *);
struct dirent *readdir(DIR *);
void          rewinddir(DIR *);

/*

    Copyright Kevlin Henney, 1997, 2003. All rights reserved.

    Permission to use, copy, modify, and distribute this software and its
    documentation for any purpose is hereby granted without fee, provided
    that this copyright and permissions notice appear in all copies and
    derivatives.
    
    This software is supplied "as is" without express or implied warranty.

    But that said, if there are any problems please get in touch.

*/

#ifdef __cplusplus
}
#endif

#endif
