/*****************************************************************************/
/*                                                                           */
/*				   symtab.h				     */
/*                                                                           */
/*		   Symbol table for the ca65 macroassembler		     */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1998-2003 Ullrich von Bassewitz                                       */
/*               Römerstraße 52                                              */
/*               D-70794 Filderstadt                                         */
/* EMail:        uz@cc65.org                                                 */
/*                                                                           */
/*                                                                           */
/* This software is provided 'as-is', without any expressed or implied       */
/* warranty.  In no event will the authors be held liable for any damages    */
/* arising from the use of this software.                                    */
/*                                                                           */
/* Permission is granted to anyone to use this software for any purpose,     */
/* including commercial applications, and to alter it and redistribute it    */
/* freely, subject to the following restrictions:                            */
/*                                                                           */
/* 1. The origin of this software must not be misrepresented; you must not   */
/*    claim that you wrote the original software. If you use this software   */
/*    in a product, an acknowledgment in the product documentation would be  */
/*    appreciated but is not required.                                       */
/* 2. Altered source versions must be plainly marked as such, and must not   */
/*    be misrepresented as being the original software.                      */
/* 3. This notice may not be removed or altered from any source              */
/*    distribution.                                                          */
/*                                                                           */
/*****************************************************************************/



#ifndef SYMTAB_H
#define SYMTAB_H



#include <stdio.h>

/* common */
#include "exprdefs.h"
#include "inline.h"

/* ca65 */
#include "symentry.h"



/*****************************************************************************/
/*   	       		      	     Data				     */
/*****************************************************************************/



/* Symbol table flags */
#define ST_NONE         0x00            /* No flags */
#define ST_DEFINED      0x01            /* Scope has been defined */

/* Symbol table types */
#define ST_GLOBAL       0x00            /* Root level */
#define ST_PROC         0x01            /* .PROC */
#define ST_SCOPE        0x02            /* .SCOPE */
#define ST_STRUCT       0x03            /* .STRUCT/.UNION */
#define ST_ENUM         0x04            /* .ENUM */
#define ST_UNDEF        0xFF

/* A symbol table */
typedef struct SymTable SymTable;
struct SymTable {
    SymTable*           Left;           /* Pointer to smaller entry */
    SymTable*           Right;          /* Pointer to greater entry */
    SymTable*          	Parent;   	/* Link to enclosing scope if any */
    SymTable*           Childs;         /* Pointer to child scopes */
    unsigned short      Flags;          /* Symbol table flags */
    unsigned char	AddrSize;       /* Address size */
    unsigned char       Type;           /* Type of the scope */
    unsigned            Level;          /* Lexical level */
    unsigned   	     	TableSlots;	/* Number of hash table slots */
    unsigned   	    	TableEntries;	/* Number of entries in the table */
    unsigned            Name;           /* Name of the scope */
    SymEntry*  	       	Table[1];   	/* Dynamic allocation */
};

/* Symbol tables */
SymTable*      	CurrentScope;   /* Pointer to current symbol table */
SymTable*	RootScope;      /* Root symbol table */



/*****************************************************************************/
/*   	       		      	     Code				     */
/*****************************************************************************/



void SymEnterLevel (const char* ScopeName, unsigned char Type, unsigned char AddrSize);
/* Enter a new lexical level */

void SymLeaveLevel (void);
/* Leave the current lexical level */

SymTable* SymFindScope (SymTable* Parent, const char* Name, int AllocNew);
/* Find a scope in the given enclosing scope */

SymTable* SymFindAnyScope (SymTable* Parent, const char* Name);
/* Find a scope in the given or any of its parent scopes. The function will
 * never create a new symbol, since this can only be done in one specific
 * scope.
 */

SymEntry* SymFindLocal (SymEntry* Parent, const char* Name, int AllocNew);
/* Find a cheap local symbol. If AllocNew is given and the entry is not
 * found, create a new one. Return the entry found, or the new entry created,
 * or - in case AllocNew is zero - return 0.
 */

SymEntry* SymFind (SymTable* Scope, const char* Name, int AllocNew);
/* Find a new symbol table entry in the given table. If AllocNew is given and
 * the entry is not found, create a new one. Return the entry found, or the
 * new entry created, or - in case AllocNew is zero - return 0.
 */

int SymIsZP (SymEntry* Sym);
/* Return true if the symbol is explicitly marked as zeropage symbol */

#if defined(HAVE_INLINE)
INLINE unsigned char GetSymTabType (const SymTable* S)
/* Return the type of the given symbol table */
{
    return S->Type;
}
#else
#  define GetSymTabType(S)      ((S)->Type)
#endif

unsigned char GetCurrentSymTabType ();
/* Return the type of the current symbol table */

void SymCheck (void);
/* Run through all symbols and check for anomalies and errors */

void SymDump (FILE* F);
/* Dump the symbol table */

void WriteImports (void);
/* Write the imports list to the object file */

void WriteExports (void);
/* Write the exports list to the object file */

void WriteDbgSyms (void);
/* Write a list of all symbols to the object file */



/* End of symtab.h */

#endif




