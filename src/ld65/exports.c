/*****************************************************************************/
/*                                                                           */
/*				   exports.c				     */
/*                                                                           */
/*		      Exports handing for the ld65 linker		     */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* (C) 1998     Ullrich von Bassewitz                                        */
/*              Wacholderweg 14                                              */
/*              D-70597 Stuttgart                                            */
/* EMail:       uz@musoftware.de                                             */
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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* common */
#include "check.h"
#include "hashstr.h"
#include "symdefs.h"
#include "xmalloc.h"
	  
/* ld65 */
#include "global.h"
#include "error.h"
#include "fileio.h"
#include "objdata.h"
#include "expr.h"
#include "exports.h"



/*****************************************************************************/
/*     	      	    		     Data				     */
/*****************************************************************************/



/* Hash table */
#define HASHTAB_SIZE	4081
static Export* 		HashTab [HASHTAB_SIZE];

/* Import management variables */
static unsigned		ImpCount = 0;	   	/* Import count */
static unsigned		ImpOpen  = 0;		/* Count of open imports */

/* Export management variables */
static unsigned		ExpCount = 0;	   	/* Export count */
static Export**		ExpPool  = 0;	  	/* Exports array */

/* Defines for the flags in Export */
#define EXP_USERMARK	0x0001



/*****************************************************************************/
/*				Import handling				     */
/*****************************************************************************/



static Export* NewExport (unsigned char Type, const char* Name, ObjData* Obj);
/* Create a new export and initialize it */



static Import* NewImport (unsigned char Type, ObjData* Obj)
/* Create a new import and initialize it */
{
    /* Allocate memory */
    Import* I = xmalloc (sizeof (Import));

    /* Initialize the fields */
    I->Next	= 0;
    I->Obj	= Obj;
    I->V.Name	= 0;
    I->Type	= Type;

    /* Return the new structure */
    return I;
}



void InsertImport (Import* I)
/* Insert an import into the table */
{
    Export* E;
    unsigned HashVal;

    /* As long as the import is not inserted, V.Name is valid */
    const char* Name = I->V.Name;

    /* Create a hash value for the given name */
    HashVal = HashStr (Name) % HASHTAB_SIZE;

    /* Search through the list in that slot and print matching duplicates */
    if (HashTab [HashVal] == 0) {
    	/* The slot is empty, we need to insert a dummy export */
    	E = HashTab [HashVal] = NewExport (0, Name, 0);
	++ExpCount;
    } else {
    	E = HashTab [HashVal];
    	while (1) {
    	    if (strcmp (E->Name, Name) == 0) {
    	  	/* We have an entry, L points to it */
       	  	break;
    	    }
    	    if (E->Next == 0) {
    	  	/* End of list an entry not found, insert a dummy */
    	  	E->Next = NewExport (0, Name, 0);
    	  	E = E->Next;		/* Point to dummy */
		++ExpCount;		/* One export more */
       	  	break;
    	    } else {
    	  	E = E->Next;
    	    }
    	}
    }

    /* Ok, E now points to a valid exports entry for the given import. Insert
     * the import into the imports list and update the counters.
     */
    I->V.Exp   = E;
    I->Next    = E->ImpList;
    E->ImpList = I;
    E->ImpCount++;
    ++ImpCount;	   		/* Total import count */
    if (E->Expr == 0) {
       	/* This is a dummy export */
    	++ImpOpen;
    }

    /* Now free the name since it's no longer needed */
    xfree (Name);
}



Import* ReadImport (FILE* F, ObjData* Obj)
/* Read an import from a file and return it */
{
    Import* I;

    /* Read the import type and check it */
    unsigned char Type = Read8 (F);
    if (Type != IMP_ZP && Type != IMP_ABS) {
	Error ("Unknown import type in module `%s': %02X", Obj->Name, Type);
    }

    /* Create a new import */
    I = NewImport (Type, Obj);

    /* Read the name */
    I->V.Name = ReadMallocedStr (F);

    /* Read the file position */
    ReadFilePos (F, &I->Pos);

    /* Return the new import */
    return I;
}



/*****************************************************************************/
/*     	      	       	   	     Code  	      	  	  	     */
/*****************************************************************************/



static Export* NewExport (unsigned char Type, const char* Name, ObjData* Obj)
/* Create a new export and initialize it */
{
    /* Get the length of the symbol name */
    unsigned Len = strlen (Name);

    /* Allocate memory */
    Export* E = xmalloc (sizeof (Export) + Len);

    /* Initialize the fields */
    E->Next     = 0;
    E->Flags	= 0;
    E->Obj      = Obj;
    E->ImpCount = 0;
    E->ImpList  = 0;
    E->Expr    	= 0;
    E->Type    	= Type;
    memcpy (E->Name, Name, Len);
    E->Name [Len] = '\0';

    /* Return the new entry */
    return E;
}



void InsertExport (Export* E)
/* Insert an exported identifier and check if it's already in the list */
{
    Export* L;
    Export* Last;
    Import* Imp;
    unsigned HashVal;

    /* Create a hash value for the given name */
    HashVal = HashStr (E->Name) % HASHTAB_SIZE;

    /* Search through the list in that slot */
    if (HashTab [HashVal] == 0) {
      	/* The slot is empty */
      	HashTab [HashVal] = E;
	++ExpCount;
    } else {

      	Last = 0;
      	L = HashTab [HashVal];
      	do {
      	    if (strcmp (L->Name, E->Name) == 0) {
      	   	/* This may be an unresolved external */
      	   	if (L->Expr == 0) {

      	   	    /* This *is* an unresolved external */
      	   	    E->Next     = L->Next;
      	   	    E->ImpCount = L->ImpCount;
      	   	    E->ImpList  = L->ImpList;
      	   	    if (Last) {
      	   	       	Last->Next = E;
      	   	    } else {
      	   	       	HashTab [HashVal] = E;
      	   	    }
       	       	    ImpOpen -= E->ImpCount;	/* Decrease open imports now */
      	   	    xfree (L);
      	   	    /* We must run through the import list and change the
      	   	     * export pointer now.
      	   	     */
      	   	    Imp = E->ImpList;
      	   	    while (Imp) {
      	   	   	Imp->V.Exp = E;
      	   	   	Imp = Imp->Next;
      	   	    }
      	   	} else {
      	   	    /* Duplicate entry, ignore it */
      	   	    Warning ("Duplicate external identifier: `%s'", L->Name);
      		}
      		return;
      	    }
      	    Last = L;
      	    L = L->Next;

      	} while (L);

	/* Insert export at end of queue */
      	Last->Next = E;
	++ExpCount;
    }
}



Export* ReadExport (FILE* F, ObjData* O)
/* Read an export from a file */
{
    unsigned char Type;
    char Name [256];
    Export* E;

    /* Read the type */
    Type = Read8 (F);

    /* Read the name */
    ReadStr (F, Name);

    /* Create a new export */
    E = NewExport (Type, Name, O);

    /* Read the value */
    if (Type & EXP_EXPR) {
       	E->Expr = ReadExpr (F, O);
    } else {
     	E->Expr = LiteralExpr (Read32 (F), O);
    }

    /* Last is the file position where the definition was done */
    ReadFilePos (F, &E->Pos);

    /* Return the new export */
    return E;
}



Export* CreateConstExport (const char* Name, long Value)
/* Create an export for a literal date */
{
    /* Create a new export */
    Export* E = NewExport (EXP_ABS, Name, 0);

    /* Assign the value */
    E->Expr = LiteralExpr (Value, 0);

    /* Insert the export */
    InsertExport (E);

    /* Return the new export */
    return E;
}



Export* CreateMemExport (const char* Name, Memory* Mem, unsigned long Offs)
/* Create an relative export for a memory area offset */
{
    /* Create a new export */
    Export* E = NewExport (EXP_ABS, Name, 0);

    /* Assign the value */
    E->Expr = MemExpr (Mem, Offs, 0);

    /* Insert the export */
    InsertExport (E);

    /* Return the new export */
    return E;
}



static Export* FindExport (const char* Name)
/* Check for an identifier in the list. Return 0 if not found, otherwise
 * return a pointer to the export.
 */
{
    /* Get a pointer to the list with the symbols hash value */
    Export* L = HashTab [HashStr (Name) % HASHTAB_SIZE];
    while (L) {
        /* Search through the list in that slot */
	if (strcmp (L->Name, Name) == 0) {
	    /* Entry found */
	    return L;
	}
	L = L->Next;
    }

    /* Not found */
    return 0;
}



int IsUnresolved (const char* Name)
/* Check if this symbol is an unresolved export */
{
    /* Find the export */
    Export* E = FindExport (Name);

    /* Check if it's unresolved */
    return E != 0 && E->Expr == 0;
}



int IsConstExport (const Export* E)
/* Return true if the expression associated with this export is const */
{
    if (E->Expr == 0) {
     	/* External symbols cannot be const */
     	return 0;
    } else {
        return IsConstExpr (E->Expr);
    }
}



long GetExportVal (const Export* E)
/* Get the value of this export */
{
    if (E->Expr == 0) {
     	/* OOPS */
       	Internal ("`%s' is an undefined external", E->Name);
    }
    return GetExprVal (E->Expr);
}



static void CheckSymType (Export* E)
/* Check the types for one export */
{
    /* External with matching imports */
    Import* Imp = E->ImpList;
    int ZP = (E->Type & EXP_ZP) != 0;
    while (Imp) {
	if (ZP != ((Imp->Type & IMP_ZP) != 0)) {
	    /* Export is ZP, import is abs or the other way round */
	    if (E->Obj) {
		/* User defined export */
		Warning ("Type mismatch for `%s', export in "
			 "%s(%lu), import in %s(%lu)",
			 E->Name, E->Obj->Files [Imp->Pos.Name],
    			 E->Pos.Line, Imp->Obj->Files [Imp->Pos.Name],
		   	 Imp->Pos.Line);
	    } else {
		/* Export created by the linker */
		Warning ("Type mismatch for `%s', imported from %s(%lu)",
			 E->Name, Imp->Obj->Files [Imp->Pos.Name],
			 Imp->Pos.Line);
	    }
	}
	Imp = Imp->Next;
    }
}



static void CheckSymTypes (void)
/* Check for symbol tape mismatches */
{
    unsigned I;

    /* Print all open imports */
    for (I = 0; I < ExpCount; ++I) {
	Export* E = ExpPool [I];
	if (E->Expr != 0 && E->ImpCount > 0) {
	    /* External with matching imports */
	    CheckSymType (E);
	}
    }
}



static void PrintUnresolved (ExpCheckFunc F, void* Data)
/* Print a list of unresolved symbols. On unresolved symbols, F is
 * called (see the comments on ExpCheckFunc in the data section).
 */
{
    unsigned I;

    /* Print all open imports */
    for (I = 0; I < ExpCount; ++I) {
	Export* E = ExpPool [I];
	if (E->Expr == 0 && E->ImpCount > 0 && F (E->Name, Data) == 0) {
	    /* Unresolved external */
	    Import* Imp = E->ImpList;
	    fprintf (stderr,
		     "Unresolved external `%s' referenced in:\n",
		     E->Name);
	    while (Imp) {
		const char* Name = Imp->Obj->Files [Imp->Pos.Name];
		fprintf (stderr, "  %s(%lu)\n", Name, Imp->Pos.Line);
		Imp = Imp->Next;
	    }
	}
    }
}



static int CmpExpName (const void* K1, const void* K2)
/* Compare function for qsort */
{
    return strcmp ((*(Export**)K1)->Name, (*(Export**)K2)->Name);
}



static void CreateExportPool (void)
/* Create an array with pointer to all exports */
{
    unsigned I, J;

    /* Allocate memory */
    if (ExpPool) {
	xfree (ExpPool);
    }
    ExpPool = xmalloc (ExpCount * sizeof (Export*));

    /* Walk through the list and insert the exports */
    for (I = 0, J = 0; I < sizeof (HashTab) / sizeof (HashTab [0]); ++I) {
	Export* E = HashTab [I];
	while (E) {
	    CHECK (J < ExpCount);
	    ExpPool [J++] = E;
	    E = E->Next;
	}
    }

    /* Sort them by name */
    qsort (ExpPool, ExpCount, sizeof (Export*), CmpExpName);
}



void CheckExports (ExpCheckFunc F, void* Data)
/* Check if there are any unresolved symbols. On unresolved symbols, F is
 * called (see the comments on ExpCheckFunc in the data section).
 */
{
    /* Create an export pool */
    CreateExportPool ();

    /* Check for symbol type mismatches */
    CheckSymTypes ();

    /* Check for unresolved externals (check here for special bin formats) */
    if (ImpOpen != 0) {
       	/* Print all open imports */
	PrintUnresolved (F, Data);
    }
}



void PrintExportMap (FILE* F)
/* Print an export map to the given file */
{
    unsigned I;
    unsigned Count;

    /* Print all exports */
    Count = 0;
    for (I = 0; I < ExpCount; ++I) {
     	Export* E = ExpPool [I];

	/* Print unreferenced symbols only if explictly requested */
	if (VerboseMap || E->ImpCount > 0) {
	    fprintf (F,
		     "%-25s %06lX %c%c    ",
		     E->Name,
	 	     GetExportVal (E),
		     E->ImpCount? 'R' : ' ',
		     (E->Type & EXP_ZP)? 'Z' : ' ');
	    if (++Count == 2) {
	 	Count = 0;
	 	fprintf (F, "\n");
	    }
	}
    }
    fprintf (F, "\n");
}



void PrintImportMap (FILE* F)
/* Print an import map to the given file */
{
    unsigned I;
    Import* Imp;

    /* Loop over all exports */
    for (I = 0; I < ExpCount; ++I) {

	/* Get the export */
     	Export* Exp = ExpPool [I];

	/* Print the symbol only if there are imports, or if a verbose map
	 * file is requested.
	 */
	if (VerboseMap || Exp->ImpCount > 0) {

	    /* Get the name of the object file that exports the symbol.
	     * Beware: There may be no object file if the symbol is a linker
	     * generated symbol.
	     */
	    const char* ObjName = (Exp->Obj != 0)? Exp->Obj->Name : "linker generated";

	    /* Print the export */
	    fprintf (F,
		     "%s (%s):\n",
		     Exp->Name,
		     ObjName);

	    /* Print all imports for this symbol */
	    Imp = Exp->ImpList;
	    while (Imp) {

		/* Print the import */
		fprintf (F,
			 "    %-25s %s(%lu)\n",
			 Imp->Obj->Name,
			 Imp->Obj->Files [Imp->Pos.Name],
			 Imp->Pos.Line);

		/* Next import */
		Imp = Imp->Next;
	    }
	}
    }
    fprintf (F, "\n");
}



void PrintExportLabels (FILE* F)
/* Print the exports in a VICE label file */
{
    unsigned I;

    /* Print all exports */
    for (I = 0; I < ExpCount; ++I) {
 	Export* E = ExpPool [I];
       	fprintf (F, "al %06lX .%s\n", GetExportVal (E), E->Name);
    }
}



void MarkExport (Export* E)
/* Mark the export */
{
    E->Flags |= EXP_USERMARK;
}



void UnmarkExport (Export* E)
/* Remove the mark from the export */
{
    E->Flags &= ~EXP_USERMARK;
}



int ExportHasMark (Export* E)
/* Return true if the export has a mark */
{
    return (E->Flags & EXP_USERMARK) != 0;
}



void CircularRefError (const Export* E)
/* Print an error about a circular reference using to define the given export */
{
    Error ("Circular reference for symbol `%s', %s(%lu)",
	   E->Name, E->Obj->Files [E->Pos.Name], E->Pos.Line);
}



