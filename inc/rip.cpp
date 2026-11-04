/////////////////////////////////////////////////////////////////////////////
// rip.cpp
/////////////////////////////////////////////////////////////////////////////
// This module contains support for communicating errors or messages to a
// developer or tester.
/////////////////////////////////////////////////////////////////////////////

#if defined(_DEBUG) || defined(DEBUG)

namespace wi {
char *gpszRipFile;
int giRipLine;
}





#ifndef RIP_DEFINED

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "inc/rip.h"

namespace wi {

void Break()
{
    printf("Break:\n");
    printf("File: %s, Line: %d\n", gpszRipFile, giRipLine);
    abort();
}

void DoRip(char *psz, va_list va)
{
    printf("Assert:\n");
    printf("File: %s, Line: %d\n", gpszRipFile, giRipLine);
    if (va == NULL) {
        printf("%s", psz);
    } else {
        vprintf(psz, va);
    }
    Break();
}

void DoAssertRip(int fNoAssert, char *psz, ...)
{
    if (fNoAssert)
        return;
    va_list va;
    va_start(va, psz);
	DoRip(psz, va);
    va_end(va);
}

void DoAssertRip(int fNoAssert)
{
	if (fNoAssert)
		return;
    va_list empty_va_list;
	DoRip("", empty_va_list);
}

void DoAssertRip(char *psz, ...)
{
    va_list va;
    va_start(va, psz);
	DoRip(psz, va);
    va_end(va);
}

void DoAssertRip()
{
	Break();
}

void dprintf(const char *psz, ...)
{
    va_list va;
    va_start(va, psz);
    vprintf(psz, va);
    va_end(va);
}

} // namespace wi
    
#endif // !RIP_DEFINED

#endif // defined(_DEBUG) || defined(DEBUG)
