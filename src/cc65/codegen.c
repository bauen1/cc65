/*****************************************************************************/
/*                                                                           */
/*				   codegen.c				     */
/*                                                                           */
/*			      6502 code generator			     */
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
#include <string.h>

#include "../common/version.h"
#include "../common/xmalloc.h"

#include "asmcode.h"
#include "asmlabel.h"
#include "check.h"
#include "cpu.h"
#include "error.h"
#include "global.h"
#include "io.h"
#include "litpool.h"
#include "optimize.h"
#include "util.h"
#include "codegen.h"



/*****************************************************************************/
/*				     Data				     */
/*****************************************************************************/



/* Compiler relative stk ptr */
int oursp	= 0;

/* Current segment */
static enum {
    SEG_INV = -1,	/* Invalid segment */
    SEG_CODE,
    SEG_RODATA,
    SEG_DATA,
    SEG_BSS
} CurSeg = SEG_CODE;

/* Segment names */
static char* SegmentNames [4];
static char* SegmentHints [4] = {
    "seg:code", "seg:rodata", "seg:data", "seg:bss"
};



/*****************************************************************************/
/*				    Helpers				     */
/*****************************************************************************/



static void typeerror (unsigned type)
/* Print an error message about an invalid operand type */
{
    Internal ("Invalid type in CF flags: %04X, type = %u", type, type & CF_TYPE);
}



static void CheckLocalOffs (unsigned Offs)
/* Check the offset into the stack for 8bit range */
{
    if (Offs >= 256) {
	/* Too many local vars */
       	AddCodeLine (";*** Too many locals");
	Error (ERR_TOO_MANY_LOCALS);
    }
}



static char* GetLabelName (unsigned flags, unsigned long label, unsigned offs)
{
    static char lbuf [128];		/* Label name */

    /* Create the correct label name */
    switch (flags & CF_ADDRMASK) {

	case CF_STATIC:
	    /* Static memory cell */
	    sprintf (lbuf, "L%04X+%u", (unsigned)(label & 0xFFFF), offs);
	    break;

	case CF_EXTERNAL:
	    /* External label */
	    sprintf (lbuf, "_%s+%u", (char*) label, offs);
	    break;

	case CF_ABSOLUTE:
	    /* Absolute address */
	    sprintf (lbuf, "$%04X", (unsigned)((label+offs) & 0xFFFF));
	    break;

	case CF_REGVAR:
	    /* Variable in register bank */
	    sprintf (lbuf, "regbank+%u", (unsigned)((label+offs) & 0xFFFF));
	    break;

	default:
	    Internal ("Invalid address flags");
    }

    /* Return a pointer to the static buffer */
    return lbuf;
}



/*****************************************************************************/
/*			      Pre- and postamble			     */
/*****************************************************************************/



void g_preamble (void)
/* Generate the assembler code preamble */
{
    AddCodeLine ("; File generated by cc65 v %u.%u.%u", VER_MAJOR, VER_MINOR, VER_PATCH);
    AddEmptyLine ();

    /* Insert some object file options */
    AddCodeLine (".fopt\t\tcompiler,\"cc65 v %u.%u.%u\"", VER_MAJOR, VER_MINOR, VER_PATCH);
    AddEmptyLine ();

    /* If we're producing code for some other CPU, switch the command set */
    if (CPU == CPU_65C02) {
	AddCodeLine (".pc02");
    }

    /* Allow auto import for runtime library routines */
    AddCodeLine (".autoimport\ton");

    /* Switch the assembler into case sensible mode */
    AddCodeLine (".case\t\ton");

    /* Tell the assembler if we want to generate debug info */
    AddCodeLine (".debuginfo\t%s", (DebugInfo != 0)? "on" : "off");

    /* Import the stack pointer for direct auto variable access */
    AddCodeLine (".importzp\tsp, sreg, regsave, regbank, tmp1, ptr1");

    /* Define long branch macros */
    AddCodeLine (".macpack\tlongbranch");
    AddEmptyLine ();

    /* Define the ldax macro */
    AddCodeLine (".macro  ldax    Value");
    AddCodeLine ("        lda     #<(Value)");
    AddCodeLine ("        ldx     #>(Value)");
    AddCodeLine (".endmacro");
    AddEmptyLine ();

    /* Define the default names for the segments */
    SegmentNames [SEG_CODE] 	= xstrdup ("CODE");
    SegmentNames [SEG_RODATA]	= xstrdup ("RODATA");
    SegmentNames [SEG_DATA]	= xstrdup ("DATA");
    SegmentNames [SEG_BSS]	= xstrdup ("BSS");

    /* Tell the optimizer that this is the end of the preamble */
    AddCodeHint ("end_of_preamble");
}



void g_postamble (void)
/* Generate assembler code postamble */
{
    /* Tell the optimizer that this is the start of the postamble */
    AddCodeHint ("start_of_postamble");
}



/*****************************************************************************/
/*	   			Segment support				     */
/*****************************************************************************/



static void UseSeg (int NewSeg)
/* Switch to a specific segment */
{
    if (CurSeg != NewSeg) {
  	CurSeg = NewSeg;
  	AddCodeLine (".segment\t\"%s\"", SegmentNames [CurSeg]);
	AddCodeHint (SegmentHints [CurSeg]);
    }
}



void g_usecode (void)
/* Switch to the code segment */
{
    UseSeg (SEG_CODE);
}



void g_userodata (void)
/* Switch to the read only data segment */
{
    UseSeg (SEG_RODATA);
}



void g_usedata (void)
/* Switch to the data segment */
{
    UseSeg (SEG_DATA);
}



void g_usebss (void)
/* Switch to the bss segment */
{
    UseSeg (SEG_BSS);
}



static void SegName (int Seg, const char* Name)
/* Set the name of a segment */
{
    /* Free the old name and set a new one */
    xfree (SegmentNames [Seg]);
    SegmentNames [Seg] = xstrdup (Name);

    /* If the new segment is the current segment, emit a segment directive
     * with the new name.
     */
    if (Seg == CurSeg) {
       	CurSeg = SEG_INV;	/* Invalidate */
	UseSeg (Seg);
    }
}



void g_codename (const char* Name)
/* Set the name of the CODE segment */
{
    SegName (SEG_CODE, Name);
}



void g_rodataname (const char* Name)
/* Set the name of the RODATA segment */
{
    SegName (SEG_RODATA, Name);
}



void g_dataname (const char* Name)
/* Set the name of the DATA segment */
{
    SegName (SEG_DATA, Name);
}



void g_bssname (const char* Name)
/* Set the name of the BSS segment */
{
    SegName (SEG_BSS, Name);
}



/*****************************************************************************/
/*  	       		 	     Code				     */
/*****************************************************************************/



unsigned sizeofarg (unsigned flags)
/* Return the size of a function argument type that is encoded in flags */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    return (flags & CF_FORCECHAR)? 1 : 2;

	case CF_INT:
	    return 2;

	case CF_LONG:
	    return 4;

	default:
	    typeerror (flags);
	    /* NOTREACHED */
	    return 2;
    }
}



int pop (unsigned flags)
/* Pop an argument of the given size */
{
    return oursp += sizeofarg (flags);
}



int push (unsigned flags)
/* Push an argument of the given size */
{
    return oursp -= sizeofarg (flags);
}



static unsigned MakeByteOffs (unsigned Flags, unsigned Offs)
/* The value in Offs is an offset to an address in a/x. Make sure, an object
 * of the type given in Flags can be loaded or stored into this address by
 * adding part of the offset to the address in ax, so that the remaining
 * offset fits into an index register. Return the remaining offset.
 */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    unsigned O = Offs & ~0xFFU;
    if ((Offs & 0xFF) > 256 - sizeofarg (Flags)) {
	/* We need to add the low byte also */
	O += Offs & 0xFF;
    }

    /* Do the correction if we need one */
    if (O != 0) {
     	g_inc (CF_INT | CF_CONST, O);
     	Offs -= O;
    }

    /* Return the new offset */
    return Offs;
}



/*****************************************************************************/
/*		  	Functions handling local labels			     */
/*****************************************************************************/



void g_defloclabel (unsigned label)
/* Define a local label */
{
    AddCodeLine ("L%04X:", label & 0xFFFF);
}



/*****************************************************************************/
/*   	     	       Functions handling global labels			     */
/*****************************************************************************/



void g_defgloblabel (const char* Name)
/* Define a global label with the given name */
{
    AddCodeLine ("_%s:", Name);
}



void g_defexport (const char* Name, int ZP)
/* Export the given label */
{
    if (ZP) {
     	AddCodeLine ("\t.exportzp\t_%s", Name);
    } else {
     	AddCodeLine ("\t.export\t\t_%s", Name);
    }
}



void g_defimport (const char* Name, int ZP)
/* Import the given label */
{
    if (ZP) {
       	AddCodeLine ("\t.importzp\t_%s", Name);
    } else {
     	AddCodeLine ("\t.import\t\t_%s", Name);
    }
}



/*****************************************************************************/
/*     		     Load functions for various registers		     */
/*****************************************************************************/



static void ldaconst (unsigned val)
/* Load a with a constant */
{
    AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
}



static void ldxconst (unsigned val)
/* Load x with a constant */
{
    AddCodeLine ("\tldx\t#$%02X", val & 0xFF);
}



static void ldyconst (unsigned val)
/* Load y with a constant */
{
    AddCodeLine ("\tldy\t#$%02X", val & 0xFF);
}



/*****************************************************************************/
/*     			    Function entry and exit			     */
/*****************************************************************************/



/* Remember the argument size of a function. The variable is set by g_enter
 * and used by g_leave. If the functions gets its argument size by the caller
 * (variable param list or function without prototype), g_enter will set the
 * value to -1.
 */
static int funcargs;


void g_enter (unsigned flags, unsigned argsize)
/* Function prologue */
{
    if ((flags & CF_FIXARGC) != 0) {
	/* Just remember the argument size for the leave */
	funcargs = argsize;
    } else {
       	funcargs = -1;
       	AddCodeLine ("\tjsr\tenter");
    }
}



void g_leave (int flags, int val)
/* Function epilogue */
{
    int k;
    char buf [40];

    /* How many bytes of locals do we have to drop? */
    k = -oursp;

    /* If we didn't have a variable argument list, don't call leave */
    if (funcargs >= 0) {

     	/* Load a function return code if needed */
     	if ((flags & CF_CONST) != 0) {
     	    g_getimmed (flags, val, 0);
     	}

     	/* Drop stackframe or leave with rts */
     	k += funcargs;
     	if (k == 0) {
     	    AddCodeLine ("\trts");
     	} else if (k <= 8) {
     	    AddCodeLine ("\tjmp\tincsp%d", k);
     	} else {
     	    CheckLocalOffs (k);
     	    ldyconst (k);
     	    AddCodeLine ("\tjmp\taddysp");
     	}

    } else {

     	strcpy (buf, "\tjmp\tleave");
     	if (k) {
     	    /* We've a stack frame to drop */
     	    ldyconst (k);
     	    strcat (buf, "y");
     	}
     	if (flags & CF_CONST) {
     	    if ((flags & CF_TYPE) != CF_LONG) {
     	   	/* Constant int sized value given for return code */
     	   	if (val == 0) {
     	   	    /* Special case: return 0 */
     	   	    strcat (buf, "00");
       	       	} else if (((val >> 8) & 0xFF) == 0) {
     	   	    /* Special case: constant with high byte zero */
     	   	    ldaconst (val);    		/* Load low byte */
     		    strcat (buf, "0");
     		} else {
     		    /* Others: arbitrary constant value */
     		    g_getimmed (flags, val, 0);	/* Load value */
     		}
     	    } else {
     		/* Constant long value: No shortcut possible */
     		g_getimmed (flags, val, 0);
     	    }
     	}

     	/* Output the jump */
     	AddCodeLine (buf);
    }

    /* Add an empty line  after a function to make the code more readable */
    AddEmptyLine ();
}



/*****************************************************************************/
/*   		       	      Register variables			     */
/*****************************************************************************/



void g_save_regvars (int RegOffs, unsigned Bytes)
/* Save register variables */
{
    /* Don't loop for up to two bytes */
    if (Bytes == 1) {

     	AddCodeLine ("\tlda\tregbank%+d", RegOffs);
       	AddCodeLine ("\tjsr\tpusha");

    } else if (Bytes == 2) {

       	AddCodeLine ("\tlda\tregbank%+d", RegOffs);
	AddCodeLine ("\tldx\tregbank%+d", RegOffs+1);
       	AddCodeLine ("\tjsr\tpushax");

    } else {

     	/* More than two bytes - loop */
     	unsigned Label = GetLabel ();
     	g_space (Bytes);
     	ldyconst (Bytes - 1);
     	ldxconst (Bytes);
     	g_defloclabel (Label);
	AddCodeLine ("\tlda\tregbank%+d,x", RegOffs-1);
	AddCodeLine ("\tsta\t(sp),y");
     	AddCodeLine ("\tdey");
     	AddCodeLine ("\tdex");
     	AddCodeLine ("\tbne\tL%04X", Label);

    }

    /* We pushed stuff, correct the stack pointer */
    oursp -= Bytes;
}



void g_restore_regvars (int StackOffs, int RegOffs, unsigned Bytes)
/* Restore register variables */
{
    /* Calculate the actual stack offset and check it */
    StackOffs -= oursp;
    CheckLocalOffs (StackOffs);

    /* Don't loop for up to two bytes */
    if (Bytes == 1) {

     	ldyconst (StackOffs);
     	AddCodeLine ("\tlda\t(sp),y");
	AddCodeLine ("\tsta\tregbank%+d", RegOffs);

    } else if (Bytes == 2) {

     	ldyconst (StackOffs);
     	AddCodeLine ("\tlda\t(sp),y");
	AddCodeLine ("\tsta\tregbank%+d", RegOffs);
	AddCodeLine ("\tiny");
	AddCodeLine ("\tlda\t(sp),y");
	AddCodeLine ("\tsta\tregbank%+d", RegOffs+1);

    } else {

     	/* More than two bytes - loop */
     	unsigned Label = GetLabel ();
     	ldyconst (StackOffs+Bytes-1);
     	ldxconst (Bytes);
     	g_defloclabel (Label);
	AddCodeLine ("\tlda\t(sp),y");
	AddCodeLine ("\tsta\tregbank%+d,x", RegOffs-1);
	AddCodeLine ("\tdey");
	AddCodeLine ("\tdex");
	AddCodeLine ("\tbne\tL%04X", Label);

    }
}



/*****************************************************************************/
/*			     Fetching memory cells	   		     */
/*****************************************************************************/



void g_getimmed (unsigned flags, unsigned long val, unsigned offs)
/* Load a constant into the primary register */
{
    if ((flags & CF_CONST) != 0) {

	/* Numeric constant */
	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if ((flags & CF_FORCECHAR) != 0) {
		    ldaconst (val);
		    break;
		}
		/* FALL THROUGH */
	    case CF_INT:
		ldxconst ((val >> 8) & 0xFF);
		ldaconst (val & 0xFF);
		break;

	    case CF_LONG:
		if (val < 0x100) {
		    AddCodeLine ("\tldx\t#$00");
		    AddCodeLine ("\tstx\tsreg+1");
		    AddCodeLine ("\tstx\tsreg");
		    AddCodeLine ("\tlda\t#$%02X", (unsigned char) val);
		} else if ((val & 0xFFFF00FF) == 0) {
		    AddCodeLine ("\tlda\t#$00");
		    AddCodeLine ("\tsta\tsreg+1");
		    AddCodeLine ("\tsta\tsreg");
		    AddCodeLine ("\tldx\t#$%02X", (unsigned char) (val >> 8));
		} else if ((val & 0xFFFF0000) == 0 && FavourSize == 0) {
		    AddCodeLine ("\tlda\t#$00");
		    AddCodeLine ("\tsta\tsreg+1");
		    AddCodeLine ("\tsta\tsreg");
		    AddCodeLine ("\tlda\t#$%02X", (unsigned char) val);
		    AddCodeLine ("\tldx\t#$%02X", (unsigned char) (val >> 8));
		} else if ((val & 0xFFFFFF00) == 0xFFFFFF00) {
		    AddCodeLine ("\tldx\t#$FF");
		    AddCodeLine ("\tstx\tsreg+1");
		    AddCodeLine ("\tstx\tsreg");
		    if ((val & 0xFF) == 0xFF) {
			AddCodeLine ("\ttxa");
		    } else {
			AddCodeLine ("\tlda\t#$%02X", (unsigned char) val);
		    }
		} else if ((val & 0xFFFF00FF) == 0xFFFF00FF) {
		    AddCodeLine ("\tlda\t#$FF");
		    AddCodeLine ("\tsta\tsreg+1");
		    AddCodeLine ("\tsta\tsreg");
		    AddCodeLine ("\tldx\t#$%02X", (unsigned char) (val >> 8));
		} else {
		    /* Call a subroutine that will load following value */
		    AddCodeLine ("\tjsr\tldeax");
		    AddCodeLine ("\t.dword\t$%08lX", val & 0xFFFFFFFF);
		}
		break;

	    default:
		typeerror (flags);
		break;

	}

    } else {

	/* Some sort of label */
	const char* Label = GetLabelName (flags, val, offs);

	/* Load the address into the primary */
	AddCodeLine ("\tldax\t%s", Label);

    }
}



void g_getstatic (unsigned flags, unsigned long label, unsigned offs)
/* Fetch an static memory cell into the primary register */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and generate the correct load operation */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	    if ((flags & CF_FORCECHAR) || (flags & CF_TEST)) {
     	        AddCodeLine ("\tlda\t%s", lbuf);	/* load A from the label */
       	    } else {
     	     	ldxconst (0);
     	     	AddCodeLine ("\tlda\t%s", lbuf);	/* load A from the label */
     	     	if (!(flags & CF_UNSIGNED)) {
     	     	    /* Must sign extend */
     	     	    AddCodeLine ("\tbpl\t*+3");
     	     	    AddCodeLine ("\tdex");
     		    AddCodeHint ("x:!");		/* X is invalid now */
     	     	}
     	    }
     	    break;

     	case CF_INT:
     	    AddCodeLine ("\tlda\t%s", lbuf);
     	    if (flags & CF_TEST) {
     		AddCodeLine ("\tora\t%s+1", lbuf);
     	    } else {
     		AddCodeLine ("\tldx\t%s+1", lbuf);
     	    }
     	    break;

   	case CF_LONG:
     	    if (flags & CF_TEST) {
	     	AddCodeLine ("\tlda\t%s+3", lbuf);
		AddCodeLine ("\tora\t%s+2", lbuf);
		AddCodeLine ("\tora\t%s+1", lbuf);
		AddCodeLine ("\tora\t%s+0", lbuf);
	    } else {
	     	AddCodeLine ("\tlda\t%s+3", lbuf);
	     	AddCodeLine ("\tsta\tsreg+1");
		AddCodeLine ("\tlda\t%s+2", lbuf);
		AddCodeLine ("\tsta\tsreg");
		AddCodeLine ("\tldx\t%s+1", lbuf);
		AddCodeLine ("\tlda\t%s", lbuf);
	    }
	    break;

       	default:
       	    typeerror (flags);

    }
}



void g_getlocal (unsigned flags, int offs)
/* Fetch specified local object (local var). */
{
    offs -= oursp;
    CheckLocalOffs (offs);
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    if ((flags & CF_FORCECHAR) || (flags & CF_TEST)) {
		if (CPU == CPU_65C02 && offs == 0) {
		    AddCodeLine ("\tlda\t(sp)");
		} else {
		    ldyconst (offs);
		    AddCodeLine ("\tlda\t(sp),y");
		}
	    } else {
		if (offs == 0) {
		    AddCodeLine ("\tldx\t#$00");
		    AddCodeLine ("\tlda\t(sp,x)");
		} else {
		    ldyconst (offs);
		    AddCodeLine ("\tldx\t#$00");
		    AddCodeLine ("\tlda\t(sp),y");
		}
     	    	if ((flags & CF_UNSIGNED) == 0) {
     	    	    AddCodeLine ("\tbpl\t*+3");
     	 	    AddCodeLine ("\tdex");
     		    AddCodeHint ("x:!");	/* X is invalid now */
	 	}
	    }
	    break;

	case CF_INT:
	    CheckLocalOffs (offs + 1);
       	    if (flags & CF_TEST) {
	    	ldyconst (offs + 1);
	    	AddCodeLine ("\tlda\t(sp),y");
		AddCodeLine ("\tdey");
		AddCodeLine ("\tora\t(sp),y");
	    } else {
		if (FavourSize) {
		    if (offs) {
			ldyconst (offs+1);
       			AddCodeLine ("\tjsr\tldaxysp");
		    } else {
			AddCodeLine ("\tjsr\tldax0sp");
		    }
		} else {
	    	    ldyconst (offs + 1);
		    AddCodeLine ("\tlda\t(sp),y");
		    AddCodeLine ("\ttax");
		    AddCodeLine ("\tdey");
		    AddCodeLine ("\tlda\t(sp),y");
		}
	    }
	    break;

	case CF_LONG:
    	    if (offs) {
    	 	ldyconst (offs+3);
    	 	AddCodeLine ("\tjsr\tldeaxysp");
    	    } else {
    	 	AddCodeLine ("\tjsr\tldeax0sp");
    	    }
	    break;

    	default:
    	    typeerror (flags);
    }
}



void g_getind (unsigned flags, unsigned offs)
/* Fetch the specified object type indirect through the primary register
 * into the primary register
 */
{
    /* If the offset is greater than 255, add the part that is > 255 to
     * the primary. This way we get an easy addition and use the low byte
     * as the offset
     */
    offs = MakeByteOffs (flags, offs);

    /* Handle the indirect fetch */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
       	    /* Character sized */
     	    if (offs) {
     		ldyconst (offs);
     	        if (flags & CF_UNSIGNED) {
     	     	    AddCodeLine ("\tjsr\tldauidx");
       	     	} else {
     	     	    AddCodeLine ("\tjsr\tldaidx");
     	     	}
     	    } else {
     	        if (flags & CF_UNSIGNED) {
     		    if (FavourSize) {
     	     	        AddCodeLine ("\tjsr\tldaui");
     		    } else {
     			AddCodeLine ("\tsta\tptr1");
     			AddCodeLine ("\tstx\tptr1+1");
     		     	AddCodeLine ("\tldx\t#$00");
     			AddCodeLine ("\tlda\t(ptr1,x)");
     		    }
     	     	} else {
     	     	    AddCodeLine ("\tjsr\tldai");
     	     	}
     	    }
     	    break;

     	case CF_INT:
     	    if (flags & CF_TEST) {
     		ldyconst (offs);
     		AddCodeLine ("\tsta\tptr1");
     		AddCodeLine ("\tstx\tptr1+1");
     		AddCodeLine ("\tlda\t(ptr1),y");
     		AddCodeLine ("\tiny");
     		AddCodeLine ("\tora\t(ptr1),y");
     	    } else {
     		if (offs == 0) {
     		    AddCodeLine ("\tjsr\tldaxi");
     		} else {
     		    ldyconst (offs+1);
     		    AddCodeLine ("\tjsr\tldaxidx");
     		}
     	    }
     	    break;

       	case CF_LONG:
     	    if (offs == 0) {
     		AddCodeLine ("\tjsr\tldeaxi");
     	    } else {
     		ldyconst (offs+3);
     		AddCodeLine ("\tjsr\tldeaxidx");
     	    }
     	    if (flags & CF_TEST) {
       		AddCodeLine ("\tjsr\ttsteax");
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_leasp (int offs)
/* Fetch the address of the specified symbol into the primary register */
{
    /* Calculate the offset relative to sp */
    offs -= oursp;

    /* For value 0 we do direct code */
    if (offs == 0) {
	AddCodeLine ("\tlda\tsp");
	AddCodeLine ("\tldx\tsp+1");
    } else {
	if (FavourSize) {
	    ldaconst (offs);         		/* Load A with offset value */
	    AddCodeLine ("\tjsr\tleaasp");	/* Load effective address */
	} else {
	    if (CPU == CPU_65C02 && offs == 1) {
	     	AddCodeLine ("\tlda\tsp");
	     	AddCodeLine ("\tldx\tsp+1");
		AddCodeLine ("\tina");
	     	AddCodeLine ("\tbne\t*+3");
	     	AddCodeLine ("\tinx");
	     	AddCodeHint ("x:!");		/* Invalidate X */
	    } else {
	     	ldaconst (offs);
	     	AddCodeLine ("\tclc");
	     	AddCodeLine ("\tldx\tsp+1");
	     	AddCodeLine ("\tadc\tsp");
	     	AddCodeLine ("\tbcc\t*+3");
	     	AddCodeLine ("\tinx");
	     	AddCodeHint ("x:!");		/* Invalidate X */
	    }
	}
    }
}



/*****************************************************************************/
/*     	    		       Store into memory			     */
/*****************************************************************************/



void g_putstatic (unsigned flags, unsigned long label, unsigned offs)
/* Store the primary register into the specified static memory cell */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and generate the correct store operation */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
    	    AddCodeLine ("\tsta\t%s", lbuf);
     	    break;

     	case CF_INT:
    	    AddCodeLine ("\tsta\t%s", lbuf);
	    AddCodeLine ("\tstx\t%s+1", lbuf);
     	    break;

     	case CF_LONG:
    	    AddCodeLine ("\tsta\t%s", lbuf);
	    AddCodeLine ("\tstx\t%s+1", lbuf);
	    AddCodeLine ("\tldy\tsreg");
	    AddCodeLine ("\tsty\t%s+2", lbuf);
	    AddCodeLine ("\tldy\tsreg+1");
	    AddCodeLine ("\tsty\t%s+3", lbuf);
     	    break;

       	default:
       	    typeerror (flags);

    }
}



void g_putlocal (unsigned flags, int offs)
/* Put data into local object. */
{
    offs -= oursp;
    CheckLocalOffs (offs);
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    if (CPU == CPU_65C02 && offs == 0) {
		AddCodeLine ("\tsta\t(sp)");
	    } else {
		ldyconst (offs);
		AddCodeLine ("\tsta\t(sp),y");
	    }
     	    break;

     	case CF_INT:
     	    if (offs) {
     	    	ldyconst (offs);
     	    	AddCodeLine ("\tjsr\tstaxysp");
     	    } else {
     	    	AddCodeLine ("\tjsr\tstax0sp");
     	    }
     	    break;

     	case CF_LONG:
     	    if (offs) {
     	     	ldyconst (offs);
     	     	AddCodeLine ("\tjsr\tsteaxysp");
     	    } else {
     	     	AddCodeLine ("\tjsr\tsteax0sp");
     	    }
     	    break;

       	default:
     	    typeerror (flags);

    }
}



void g_putind (unsigned flags, unsigned offs)
/* Store the specified object type in the primary register at the address
 * on the top of the stack
 */
{
    /* We cannot currently handle more than byte sized offsets */
    if (offs > 256 - sizeofarg (flags)) {
	Internal ("g_putind: Large offsets not implemented");
    }

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	    if (offs) {
     	        ldyconst (offs);
     	       	AddCodeLine ("\tjsr\tstaspidx");
     	    } else {
     	    	AddCodeLine ("\tjsr\tstaspp");
     	    }
     	    break;

     	case CF_INT:
     	    if (offs) {
     	        ldyconst (offs);
     		AddCodeLine ("\tjsr\tstaxspidx");
     	    } else {
     		AddCodeLine ("\tjsr\tstaxspp");
     	    }
     	    break;

     	case CF_LONG:
     	    if (offs) {
     	        ldyconst (offs);
     		AddCodeLine ("\tjsr\tsteaxspidx");
     	    } else {
     		AddCodeLine ("\tjsr\tsteaxspp");
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }

    /* Pop the argument which is always a pointer */
    pop (CF_PTR);
}



/*****************************************************************************/
/*		      type conversion and similiar stuff		     */
/*****************************************************************************/



void g_toslong (unsigned flags)
/* Make sure, the value on TOS is a long. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    if (flags & CF_UNSIGNED) {
		AddCodeLine ("\tjsr\ttosulong");
	    } else {
		AddCodeLine ("\tjsr\ttoslong");
	    }
	    push (CF_INT);
	    break;

	case CF_LONG:
	    break;

	default:
	    typeerror (flags);
    }
}



void g_tosint (unsigned flags)
/* Make sure, the value on TOS is an int. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\ttosint");
	    pop (CF_INT);
	    break;

	default:
	    typeerror (flags);
    }
}



void g_reglong (unsigned flags)
/* Make sure, the value in the primary register a long. Convert if necessary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    if (flags & CF_UNSIGNED) {
	    	if (FavourSize) {
	       	    AddCodeLine ("\tjsr\taxulong");
	    	} else {
	    	    ldyconst (0);
	    	    AddCodeLine ("\tsty\tsreg");
		    AddCodeLine ("\tsty\tsreg+1");
	    	}
	    } else {
	    	AddCodeLine ("\tjsr\taxlong");
	    }
	    break;

	case CF_LONG:
	    break;

	default:
	    typeerror (flags);
    }
}



unsigned g_typeadjust (unsigned lhs, unsigned rhs)
/* Adjust the integer operands before doing a binary operation. lhs is a flags
 * value, that corresponds to the value on TOS, rhs corresponds to the value
 * in (e)ax. The return value is the the flags value for the resulting type.
 */
{
    unsigned ltype, rtype;
    unsigned result;

    /* Get the type spec from the flags */
    ltype = lhs & CF_TYPE;
    rtype = rhs & CF_TYPE;

    /* Check if a conversion is needed */
    if (ltype == CF_LONG && rtype != CF_LONG && (rhs & CF_CONST) == 0) {
   	/* We must promote the primary register to long */
   	g_reglong (rhs);
   	/* Get the new rhs type */
   	rhs = (rhs & ~CF_TYPE) | CF_LONG;
   	rtype = CF_LONG;
    } else if (ltype != CF_LONG && (lhs & CF_CONST) == 0 && rtype == CF_LONG) {
   	/* We must promote the lhs to long */
	if (lhs & CF_REG) {
	    g_reglong (lhs);
	} else {
   	    g_toslong (lhs);
	}
   	/* Get the new rhs type */
   	lhs = (lhs & ~CF_TYPE) | CF_LONG;
   	ltype = CF_LONG;
    }

    /* Determine the result type for the operation:
     *	- The result is const if both operands are const.
     *	- The result is unsigned if one of the operands is unsigned.
     *	- The result is long if one of the operands is long.
     *	- Otherwise the result is int sized.
     */
    result = (lhs & CF_CONST) & (rhs & CF_CONST);
    result |= (lhs & CF_UNSIGNED) | (rhs & CF_UNSIGNED);
    if (rtype == CF_LONG || ltype == CF_LONG) {
	result |= CF_LONG;
    } else {
	result |= CF_INT;
    }
    return result;
}



unsigned g_typecast (unsigned lhs, unsigned rhs)
/* Cast the value in the primary register to the operand size that is flagged
 * by the lhs value. Return the result value.
 */
{
    unsigned ltype, rtype;

    /* Get the type spec from the flags */
    ltype = lhs & CF_TYPE;
    rtype = rhs & CF_TYPE;

    /* Check if a conversion is needed */
    if (ltype == CF_LONG && rtype != CF_LONG && (rhs & CF_CONST) == 0) {
	/* We must promote the primary register to long */
	g_reglong (rhs);
    }

    /* Do not need any other action. If the left type is int, and the primary
     * register is long, it will be automagically truncated. If the right hand
     * side is const, it is not located in the primary register and handled by
     * the expression parser code.
     */

    /* Result is const if the right hand side was const */
    lhs |= (rhs & CF_CONST);

    /* The resulting type is that of the left hand side (that's why you called
     * this function :-)
     */
    return lhs;
}



void g_scale (unsigned flags, long val)
/* Scale the value in the primary register by the given value. If val is positive,
 * scale up, is val is negative, scale down. This function is used to scale
 * the operands or results of pointer arithmetic by the size of the type, the
 * pointer points to.
 */
{
    int p2;

    /* Value may not be zero */
    if (val == 0) {
       	Internal ("Data type has no size");
    } else if (val > 0) {

     	/* Scale up */
     	if ((p2 = powerof2 (val)) > 0 && p2 <= 3) {

     	    /* Factor is 2, 4 or 8, use special function */
     	    switch (flags & CF_TYPE) {

     		case CF_CHAR:
     		    if (flags & CF_FORCECHAR) {
     		     	while (p2--) {
     		     	    AddCodeLine ("\tasl\ta");
     	     	     	}
     	     	     	break;
     	     	    }
     	     	    /* FALLTHROUGH */

     	     	case CF_INT:
     	     	    if (FavourSize || p2 >= 3) {
     	     	       	if (flags & CF_UNSIGNED) {
     	     	     	    AddCodeLine ("\tjsr\tshlax%d", p2);
     	     	     	} else {
     	     	     	    AddCodeLine ("\tjsr\taslax%d", p2);
     	     	     	}
     	     	    } else {
     	     		AddCodeLine ("\tstx\ttmp1");
     	     	  	while (p2--) {
     	     		    AddCodeLine ("\tasl\ta");
	     		    AddCodeLine ("\trol\ttmp1");
     	     		}
     	     		AddCodeLine ("\tldx\ttmp1");
     	     	    }
     	     	    break;

     	     	case CF_LONG:
     	     	    if (flags & CF_UNSIGNED) {
     	     	     	AddCodeLine ("\tjsr\tshleax%d", p2);
     	     	    } else {
     	     		AddCodeLine ("\tjsr\tasleax%d", p2);
     	     	    }
     	     	    break;

     		default:
     		    typeerror (flags);

     	    }

     	} else if (val != 1) {

       	    /* Use a multiplication instead */
     	    g_mul (flags | CF_CONST, val);

     	}

    } else {

     	/* Scale down */
     	val = -val;
     	if ((p2 = powerof2 (val)) > 0 && p2 <= 3) {

     	    /* Factor is 2, 4 or 8, use special function */
     	    switch (flags & CF_TYPE) {

     		case CF_CHAR:
     		    if (flags & CF_FORCECHAR) {
     			if (flags & CF_UNSIGNED) {
     			    while (p2--) {
     			      	AddCodeLine ("\tlsr\ta");
     			    }
     			    break;
     			} else if (p2 <= 2) {
     		  	    AddCodeLine ("\tcmp\t#$80");
     			    AddCodeLine ("\tror\ta");
     			    break;
     			}
     		    }
     		    /* FALLTHROUGH */

     		case CF_INT:
     		    if (flags & CF_UNSIGNED) {
			if (FavourSize || p2 >= 3) {
     			    AddCodeLine ("\tjsr\tlsrax%d", p2);
			} else {
			    AddCodeLine ("\tstx\ttmp1");
			    while (p2--) {
	     		    	AddCodeLine ("\tlsr\ttmp1");
				AddCodeLine ("\tror\ta");
			    }
			    AddCodeLine ("\tldx\ttmp1");
			}
     		    } else {
			if (FavourSize || p2 >= 3) {
     			    AddCodeLine ("\tjsr\tasrax%d", p2);
			} else {
			    AddCodeLine ("\tstx\ttmp1");
			    while (p2--) {
			    	AddCodeLine ("\tcpx\t#$80");
    			    	AddCodeLine ("\tror\ttmp1");
			    	AddCodeLine ("\tror\ta");
			    }
			    AddCodeLine ("\tldx\ttmp1");
	    	     	}
     		    }
     		    break;

     		case CF_LONG:
     		    if (flags & CF_UNSIGNED) {
     		     	AddCodeLine ("\tjsr\tlsreax%d", p2);
     		    } else {
     		       	AddCodeLine ("\tjsr\tasreax%d", p2);
     		    }
     		    break;

     		default:
     		    typeerror (flags);

     	    }

     	} else if (val != 1) {

       	    /* Use a division instead */
     	    g_div (flags | CF_CONST, val);

     	}
    }
}



/*****************************************************************************/
/*	     	Adds and subs of variables fix a fixed address		     */
/*****************************************************************************/



void g_addlocal (unsigned flags, int offs)
/* Add a local variable to ax */
{
    /* Correct the offset and check it */
    offs -= oursp;
    CheckLocalOffs (offs);

    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    AddCodeLine ("\tldy\t#$%02X", offs & 0xFF);
	    AddCodeLine ("\tclc");
	    AddCodeLine ("\tadc\t(sp),y");
	    AddCodeLine ("\tbcc\t*+3");
	    AddCodeLine ("\tinx");
	    AddCodeHint ("x:!");
	    break;

     	case CF_INT:
     	    AddCodeLine ("\tldy\t#$%02X", offs & 0xFF);
     	    AddCodeLine ("\tclc");
     	    AddCodeLine ("\tadc\t(sp),y");
     	    AddCodeLine ("\tpha");
     	    AddCodeLine ("\ttxa");
     	    AddCodeLine ("\tiny");
     	    AddCodeLine ("\tadc\t(sp),y");
     	    AddCodeLine ("\ttax");
     	    AddCodeLine ("\tpla");
     	    break;

     	case CF_LONG:
     	    /* Do it the old way */
       	    g_push (flags, 0);
     	    g_getlocal (flags, offs);
     	    g_add (flags, 0);
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_addstatic (unsigned flags, unsigned long label, unsigned offs)
/* Add a static variable to ax */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    AddCodeLine ("\tclc");
	    AddCodeLine ("\tadc\t%s", lbuf);
	    AddCodeLine ("\tbcc\t*+3");
	    AddCodeLine ("\tinx");
	    AddCodeHint ("x:!");
	    break;

	case CF_INT:
	    AddCodeLine ("\tclc");
	    AddCodeLine ("\tadc\t%s", lbuf);
	    AddCodeLine ("\ttay");
	    AddCodeLine ("\ttxa");
     	    AddCodeLine ("\tadc\t%s+1", lbuf);
	    AddCodeLine ("\ttax");
	    AddCodeLine ("\ttya");
	    break;

	case CF_LONG:
	    /* Do it the old way */
       	    g_push (flags, 0);
	    g_getstatic (flags, label, offs);
	    g_add (flags, 0);
	    break;

	default:
	    typeerror (flags);

    }
}



/*****************************************************************************/
/*	       Compares of ax with a variable with fixed address	     */
/*****************************************************************************/



void g_cmplocal (unsigned flags, int offs)
/* Compare a local variable to ax */
{
    Internal ("g_cmplocal not implemented");
}



void g_cmpstatic (unsigned flags, unsigned label, unsigned offs)
/* Compare a static variable to ax */
{
    Internal ("g_cmpstatic not implemented");
}



/*****************************************************************************/
/*   	    	       	     Special op= functions			     */
/*****************************************************************************/



void g_addeqstatic (unsigned flags, unsigned long label, unsigned offs,
       	    	    unsigned long val)
/* Emit += for a static variable */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
     	       	AddCodeLine ("\tldx\t#$00");
       	    	if (flags & CF_CONST) {
     	    	    if (val == 1) {
     	    	   	AddCodeLine ("\tinc\t%s", lbuf);
     	     	   	AddCodeLine ("\tlda\t%s", lbuf);
     	     	    } else {
       	       	       	AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
     	     	   	AddCodeLine ("\tclc");
     	     	   	AddCodeLine ("\tadc\t%s", lbuf);
     	     		AddCodeLine ("\tsta\t%s", lbuf);
     	     	    }
       	       	} else {
     	     	    AddCodeLine ("\tclc");
       	     	    AddCodeLine ("\tadc\t%s", lbuf);
     	     	    AddCodeLine ("\tsta\t%s", lbuf);
       	     	}
     	    	if ((flags & CF_UNSIGNED) == 0) {
     	    	    AddCodeLine ("\tbpl\t*+3");
     		    AddCodeLine ("\tdex");
     		    AddCodeHint ("x:!");	       	/* Invalidate X */
     		}
       		break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
       	    if (flags & CF_CONST) {
     		if (val == 1) {
     		    label = GetLabel ();
     		    AddCodeLine ("\tinc\t%s", lbuf);
     		    AddCodeLine ("\tbne\tL%04X", label);
     		    AddCodeLine ("\tinc\t%s+1", lbuf);
     		    g_defloclabel (label);
     		    AddCodeLine ("\tlda\t%s", lbuf);		/* Hmmm... */
     		    AddCodeLine ("\tldx\t%s+1", lbuf);
     		} else {
       	       	    AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
     		    AddCodeLine ("\tclc");
     		    AddCodeLine ("\tadc\t%s", lbuf);
     		    AddCodeLine ("\tsta\t%s", lbuf);
     		    if (val < 0x100) {
     		       	label = GetLabel ();
     		       	AddCodeLine ("\tbcc\tL%04X", label);
     		       	AddCodeLine ("\tinc\t%s+1", lbuf);
       		       	g_defloclabel (label);
     		       	AddCodeLine ("\tldx\t%s+1", lbuf);
     		    } else {
       	       	       	AddCodeLine ("\tlda\t#$%02X", (val >> 8) & 0xFF);
     		       	AddCodeLine ("\tadc\t%s+1", lbuf);
     		       	AddCodeLine ("\tsta\t%s+1", lbuf);
     		       	AddCodeLine ("\ttax");
     		       	AddCodeLine ("\tlda\t%s", lbuf);
     		    }
     		}
       	    } else {
     		AddCodeLine ("\tclc");
       		AddCodeLine ("\tadc\t%s", lbuf);
       		AddCodeLine ("\tsta\t%s", lbuf);
       		AddCodeLine ("\ttxa");
       		AddCodeLine ("\tadc\t%s+1", lbuf);
       	     	AddCodeLine ("\tsta\t%s+1", lbuf);
       	     	AddCodeLine ("\ttax");
	     	AddCodeLine ("\tlda\t%s", lbuf);
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
		if (val < 0x100) {
		    AddCodeLine ("\tldy\t#<(%s)", lbuf);
		    AddCodeLine ("\tsty\tptr1");
		    AddCodeLine ("\tldy\t#>(%s+1)", lbuf);
		    if (val == 1) {
			AddCodeLine ("\tjsr\tladdeq1");
		    } else {
			AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
		     	AddCodeLine ("\tjsr\tladdeqa");
		    }
		} else {
		    g_getstatic (flags, label, offs);
		    g_inc (flags, val);
		    g_putstatic (flags, label, offs);
		}
	    } else {
		AddCodeLine ("\tldy\t#<(%s)", lbuf);
		AddCodeLine ("\tsty\tptr1");
		AddCodeLine ("\tldy\t#>(%s+1)", lbuf);
		AddCodeLine ("\tjsr\tladdeq");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_addeqlocal (unsigned flags, int offs, unsigned long val)
/* Emit += for a local variable */
{
    /* Calculate the true offset, check it, load it into Y */
    offs -= oursp;
    CheckLocalOffs (offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
       	     	if (offs == 0) {
       	     	    AddCodeLine ("\tldx\t#$00");
       	     	    if (flags & CF_CONST) {
       	     	       	AddCodeLine ("\tclc");
       	       	       	AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
       	     	       	AddCodeLine ("\tadc\t(sp,x)");
       	     	       	AddCodeLine ("\tsta\t(sp,x)");
       	     	    } else {
       	     	       	AddCodeLine ("\tclc");
       	     	       	AddCodeLine ("\tadc\t(sp,x)");
       	     	       	AddCodeLine ("\tsta\t(sp,x)");
       	     	    }
       	     	} else {
       	     	    ldyconst (offs);
     	     	    AddCodeLine ("\tldx\t#$00");
     	     	    if (flags & CF_CONST) {
     	     	       	AddCodeLine ("\tclc");
       	       	       	AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
     	     		AddCodeLine ("\tadc\t(sp),y");
     	     		AddCodeLine ("\tsta\t(sp),y");
     	     	    } else {
     	     	 	AddCodeLine ("\tclc");
     	     		AddCodeLine ("\tadc\t(sp),y");
     	     		AddCodeLine ("\tsta\t(sp),y");
     	     	    }
     	     	}
     	     	if ((flags & CF_UNSIGNED) == 0) {
     	     	    AddCodeLine ("\tbpl\t*+3");
     	     	    AddCodeLine ("\tdex");
     	     	    AddCodeHint ("x:!");	/* Invalidate X */
     	     	}
       	     	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
     	    if (flags & CF_CONST) {
     	     	g_getimmed (flags, val, 0);
     	    }
     	    if (offs == 0) {
     	     	AddCodeLine ("\tjsr\taddeq0sp");
     	    } else {
     	     	ldyconst (offs);
     	     	AddCodeLine ("\tjsr\taddeqysp");
     	    }
       	    break;

       	case CF_LONG:
     	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
		AddCodeLine ("\tjsr\tladdeq0sp");
	    } else {
		ldyconst (offs);
		AddCodeLine ("\tjsr\tladdeqysp");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_addeqind (unsigned flags, unsigned offs, unsigned long val)
/* Emit += for the location with address in ax */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    offs = MakeByteOffs (flags, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
	    AddCodeLine ("\tsta\tptr1");
	    AddCodeLine ("\tstx\tptr1+1");
	    if (offs == 0) {
		AddCodeLine ("\tldx\t#$00");
		AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
		AddCodeLine ("\tclc");
		AddCodeLine ("\tadc\t(ptr1,x)");
		AddCodeLine ("\tsta\t(ptr1,x)");
	    } else {
		AddCodeLine ("\tldy\t#$%02X", offs);
       	       	AddCodeLine ("\tldx\t#$00");
       	       	AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
       	       	AddCodeLine ("\tclc");
       	       	AddCodeLine ("\tadc\t(ptr1),y");
       	       	AddCodeLine ("\tsta\t(ptr1),y");
	    }
     	    break;

       	case CF_INT:
	    if (!FavourSize) {
		/* Lots of code, use only if size is not important */
       	       	AddCodeLine ("\tsta\tptr1");
		AddCodeLine ("\tstx\tptr1+1");
		AddCodeLine ("\tldy\t#$%02X", offs);
		AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
		AddCodeLine ("\tclc");
		AddCodeLine ("\tadc\t(ptr1),y");
		AddCodeLine ("\tsta\t(ptr1),y");
		AddCodeLine ("\tpha");
		AddCodeLine ("\tiny");
		AddCodeLine ("\tlda\t#$%02X", (val >> 8) & 0xFF);
		AddCodeLine ("\tadc\t(ptr1),y");
		AddCodeLine ("\tsta\t(ptr1),y");
		AddCodeLine ("\ttax");
		AddCodeLine ("\tpla");
		break;
	    }
	    /* FALL THROUGH */

       	case CF_LONG:
       	    AddCodeLine ("\tjsr\tpushax");  	/* Push the address */
	    push (flags);		    	/* Correct the internal sp */
	    g_getind (flags, offs);		/* Fetch the value */
	    g_inc (flags, val);	   		/* Increment value in primary */
	    g_putind (flags, offs);		/* Store the value back */
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqstatic (unsigned flags, unsigned long label, unsigned offs,
       		    unsigned long val)
/* Emit -= for a static variable */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
       		AddCodeLine ("\tldx\t#$00");
       	  	if (flags & CF_CONST) {
       		    if (val == 1) {
       			AddCodeLine ("\tdec\t%s", lbuf);
       			AddCodeLine ("\tlda\t%s", lbuf);
       		    } else {
       		       	AddCodeLine ("\tsec");
       		     	AddCodeLine ("\tlda\t%s", lbuf);
       		     	AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
       		     	AddCodeLine ("\tsta\t%s", lbuf);
       		    }
       	  	} else {
       		    AddCodeLine ("\tsec");
       		    AddCodeLine ("\tsta\ttmp1");
       	  	    AddCodeLine ("\tlda\t%s", lbuf);
       	       	    AddCodeLine ("\tsbc\ttmp1");
       		    AddCodeLine ("\tsta\t%s", lbuf);
       	  	}
       		if ((flags & CF_UNSIGNED) == 0) {
       		    AddCodeLine ("\tbpl\t*+3");
       		    AddCodeLine ("\tdex");
       		    AddCodeHint ("x:!");	       	/* Invalidate X */
       	     	}
       	  	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
	    AddCodeLine ("\tsec");
	    if (flags & CF_CONST) {
	       	AddCodeLine ("\tlda\t%s", lbuf);
	  	AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
	  	AddCodeLine ("\tsta\t%s", lbuf);
	   	if (val < 0x100) {
	  	    label = GetLabel ();
	  	    AddCodeLine ("\tbcs\tL%04X", label);
		    AddCodeLine ("\tdec\t%s+1", lbuf);
		    g_defloclabel (label);
		    AddCodeLine ("\tldx\t%s+1", lbuf);
		} else {
		    AddCodeLine ("\tlda\t%s+1", lbuf);
		    AddCodeLine ("\tsbc\t#$%02X", (val >> 8) & 0xFF);
		    AddCodeLine ("\tsta\t%s+1", lbuf);
		    AddCodeLine ("\ttax");
		    AddCodeLine ("\tlda\t%s", lbuf);
		}
	    } else {
		AddCodeLine ("\tsta\ttmp1");
		AddCodeLine ("\tlda\t%s", lbuf);
	        AddCodeLine ("\tsbc\ttmp1");
		AddCodeLine ("\tsta\t%s", lbuf);
       	       	AddCodeLine ("\tstx\ttmp1");
		AddCodeLine ("\tlda\t%s+1", lbuf);
		AddCodeLine ("\tsbc\ttmp1");
		AddCodeLine ("\tsta\t%s+1", lbuf);
		AddCodeLine ("\ttax");
		AddCodeLine ("\tlda\t%s", lbuf);
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
		if (val < 0x100) {
		    AddCodeLine ("\tldy\t#<(%s)", lbuf);
		    AddCodeLine ("\tsty\tptr1");
		    AddCodeLine ("\tldy\t#>(%s+1)", lbuf);
		    if (val == 1) {
	     		AddCodeLine ("\tjsr\tlsubeq1");
		    } else {
			AddCodeLine ("\tlda\t#$%02X", val & 0xFF);
			AddCodeLine ("\tjsr\tlsubeqa");
		    }
		} else {
		    g_getstatic (flags, label, offs);
		    g_dec (flags, val);
		    g_putstatic (flags, label, offs);
		}
	    } else {
		AddCodeLine ("\tldy\t#<(%s)", lbuf);
		AddCodeLine ("\tsty\tptr1");
		AddCodeLine ("\tldy\t#>(%s+1)", lbuf);
		AddCodeLine ("\tjsr\tlsubeq");
       	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqlocal (unsigned flags, int offs, unsigned long val)
/* Emit -= for a local variable */
{
    /* Calculate the true offset, check it, load it into Y */
    offs -= oursp;
    CheckLocalOffs (offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
       	    if (flags & CF_FORCECHAR) {
    	 	ldyconst (offs);
		AddCodeLine ("\tldx\t#$00");
       	 	AddCodeLine ("\tsec");
		if (flags & CF_CONST) {
		    AddCodeLine ("\tlda\t(sp),y");
		    AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
		} else {
		    AddCodeLine ("\tsta\ttmp1");
	     	    AddCodeLine ("\tlda\t(sp),y");
		    AddCodeLine ("\tsbc\ttmp1");
		}
       	 	AddCodeLine ("\tsta\t(sp),y");
		if ((flags & CF_UNSIGNED) == 0) {
	       	    AddCodeLine ("\tbpl\t*+3");
		    AddCodeLine ("\tdex");
		    AddCodeHint ("x:!");		/* Invalidate X */
		}
       	 	break;
       	    }
       	    /* FALLTHROUGH */

       	case CF_INT:
	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
	 	AddCodeLine ("\tjsr\tsubeq0sp");
	    } else {
	 	ldyconst (offs);
	 	AddCodeLine ("\tjsr\tsubeqysp");
	    }
       	    break;

       	case CF_LONG:
	    if (flags & CF_CONST) {
	     	g_getimmed (flags, val, 0);
	    }
	    if (offs == 0) {
		AddCodeLine ("\tjsr\tlsubeq0sp");
	    } else {
		ldyconst (offs);
		AddCodeLine ("\tjsr\tlsubeqysp");
	    }
       	    break;

       	default:
       	    typeerror (flags);
    }
}



void g_subeqind (unsigned flags, unsigned offs, unsigned long val)
/* Emit -= for the location with address in ax */
{
    /* If the offset is too large for a byte register, add the high byte
     * of the offset to the primary. Beware: We need a special correction
     * if the offset in the low byte will overflow in the operation.
     */
    offs = MakeByteOffs (flags, offs);

    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

       	case CF_CHAR:
	    AddCodeLine ("\tsta\tptr1");
	    AddCodeLine ("\tstx\tptr1+1");
	    if (offs == 0) {
	 	AddCodeLine ("\tldx\t#$00");
	       	AddCodeLine ("\tlda\t(ptr1,x)");
       	       	AddCodeLine ("\tsec");
	 	AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
	 	AddCodeLine ("\tsta\t(ptr1,x)");
	    } else {
       	       	AddCodeLine ("\tldy\t#$%02X", offs);
	 	AddCodeLine ("\tldx\t#$00");
	 	AddCodeLine ("\tlda\t(ptr1),y");
	 	AddCodeLine ("\tsec");
	 	AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
		AddCodeLine ("\tsta\t(ptr1),y");
	    }
     	    break;

       	case CF_INT:
	    if (!FavourSize) {
		/* Lots of code, use only if size is not important */
		AddCodeLine ("\tsta\tptr1");
       	       	AddCodeLine ("\tstx\tptr1+1");
		AddCodeLine ("\tldy\t#$%02X", offs);
		AddCodeLine ("\tlda\t(ptr1),y");
		AddCodeLine ("\tsec");
		AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
		AddCodeLine ("\tsta\t(ptr1),y");
		AddCodeLine ("\tpha");
		AddCodeLine ("\tiny");
		AddCodeLine ("\tlda\t(ptr1),y");
		AddCodeLine ("\tsbc\t#$%02X", (val >> 8) & 0xFF);
		AddCodeLine ("\tsta\t(ptr1),y");
	     	AddCodeLine ("\ttax");
		AddCodeLine ("\tpla");
		break;
	    }
	    /* FALL THROUGH */

       	case CF_LONG:
       	    AddCodeLine ("\tjsr\tpushax");     	/* Push the address */
	    push (flags);  			/* Correct the internal sp */
	    g_getind (flags, offs);		/* Fetch the value */
	    g_dec (flags, val);			/* Increment value in primary */
	    g_putind (flags, offs);		/* Store the value back */
       	    break;

       	default:
       	    typeerror (flags);
    }
}



/*****************************************************************************/
/*		   Add a variable address to the value in ax		     */
/*****************************************************************************/



void g_addaddr_local (unsigned flags, int offs)
/* Add the address of a local variable to ax */
{
    /* Add the offset */
    offs -= oursp;
    if (offs != 0) {
	/* We cannot address more then 256 bytes of locals anyway */
	CheckLocalOffs (offs);
	AddCodeLine ("\tclc");
	AddCodeLine ("\tadc\t#$%02X", offs & 0xFF);
       	AddCodeLine ("\tbcc\t*+4");	/* Do also skip the CLC insn below */
	AddCodeLine ("\tinx");
	AddCodeHint ("x:!");    	       	/* Invalidate X */
    }

    /* Add the current stackpointer value */
    AddCodeLine ("\tclc");
    AddCodeLine ("\tadc\tsp");
    AddCodeLine ("\ttay");
    AddCodeLine ("\ttxa");
    AddCodeLine ("\tadc\tsp+1");
    AddCodeLine ("\ttax");
    AddCodeLine ("\ttya");
}



void g_addaddr_static (unsigned flags, unsigned long label, unsigned offs)
/* Add the address of a static variable to ax */
{
    /* Create the correct label name */
    char* lbuf = GetLabelName (flags, label, offs);

    /* Add the address to the current ax value */
    AddCodeLine ("\tclc");
    AddCodeLine ("\tadc\t#<(%s)", lbuf);
    AddCodeLine ("\ttay");
    AddCodeLine ("\ttxa");
    AddCodeLine ("\tadc\t#>(%s)", lbuf);
    AddCodeLine ("\ttax");
    AddCodeLine ("\ttya");
}



/*****************************************************************************/
/*			  	     					     */
/*****************************************************************************/



void g_save (unsigned flags)
/* Copy primary register to hold register. */
{
    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    if (flags & CF_FORCECHAR) {
	     	AddCodeLine ("\tpha");
		break;
	    }
	    /* FALLTHROUGH */

	case CF_INT:
	    AddCodeLine ("\tsta\tregsave");
	    AddCodeLine ("\tstx\tregsave+1");
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\tsaveeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_restore (unsigned flags)
/* Copy hold register to P. */
{
    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    if (flags & CF_FORCECHAR) {
	       	AddCodeLine ("\tpla");
	    	break;
	    }
	    /* FALLTHROUGH */

	case CF_INT:
	    AddCodeLine ("\tlda\tregsave");
	    AddCodeLine ("\tldx\tregsave+1");
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\tresteax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_cmp (unsigned flags, unsigned long val)
/* Immidiate compare. The primary register will not be changed, Z flag
 * will be set.
 */
{
    /* Check the size and determine operation */
    switch (flags & CF_TYPE) {

      	case CF_CHAR:
     	    if (flags & CF_FORCECHAR) {
	       	AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
     	    	break;
     	    }
     	    /* FALLTHROUGH */

     	case CF_INT:
	    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
       	    AddCodeLine ("\tbne\t*+4");
	    AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
     	    break;

        case CF_LONG:
	    Internal ("g_cmp: Long compares not implemented");
	    break;

	default:
	    typeerror (flags);
    }
}



static void oper (unsigned flags, unsigned long val, char** subs)
/* Encode a binary operation. subs is a pointer to four groups of three
 * strings:
 *	0-2	--> Operate on ints
 *	3-5	--> Operate on unsigneds
 *	6-8	--> Operate on longs
 *	9-11	--> Operate on unsigned longs
 *
 * The first subroutine names in each string group is used to encode an
 * operation with a zero constant, the second to encode an operation with
 * a 8 bit constant, and the third is used in all other cases.
 */
{
    unsigned offs;

    /* Determine the offset into the array */
    offs = (flags & CF_UNSIGNED)? 3 : 0;
    switch (flags & CF_TYPE) {
 	case CF_CHAR:
 	case CF_INT:
 	    break;

 	case CF_LONG:
 	    offs += 6;
 	    break;

 	default:
 	    typeerror (flags);
    }

    /* Encode the operation */
    if (flags & CF_CONST) {
 	/* Constant value given */
 	if (val == 0 && subs [offs+0]) {
 	    /* Special case: constant with value zero */
 	    AddCodeLine ("\tjsr\t%s", subs [offs+0]);
 	} else if (val < 0x100 && subs [offs+1]) {
 	    /* Special case: constant with high byte zero */
 	    ldaconst (val);		/* Load low byte */
 	    AddCodeLine ("\tjsr\t%s", subs [offs+1]);
 	} else {
 	    /* Others: arbitrary constant value */
 	    g_getimmed (flags, val, 0);   	       	/* Load value */
 	    AddCodeLine ("\tjsr\t%s", subs [offs+2]);
 	}
    } else {
 	/* Value not constant (is already in (e)ax) */
 	AddCodeLine ("\tjsr\t%s", subs [offs+2]);
    }

    /* The operation will pop it's argument */
    pop (flags);
}



void g_test (unsigned flags)
/* Force a test to set cond codes right */
{
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
 	    if (flags & CF_FORCECHAR) {
 		AddCodeLine ("\ttax");
 		break;
 	    }
 	    /* FALLTHROUGH */

     	case CF_INT:
 	    AddCodeLine ("\tstx\ttmp1");
 	    AddCodeLine ("\tora\ttmp1");
     	    break;

     	case CF_LONG:
     	    if (flags & CF_UNSIGNED) {
     	    	AddCodeLine ("\tjsr\tutsteax");
     	    } else {
     	    	AddCodeLine ("\tjsr\ttsteax");
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_push (unsigned flags, unsigned long val)
/* Push the primary register or a constant value onto the stack */
{
    unsigned char hi;

    if (flags & CF_CONST && (flags & CF_TYPE) != CF_LONG) {

     	/* We have a constant 8 or 16 bit value */
     	if ((flags & CF_TYPE) == CF_CHAR && (flags & CF_FORCECHAR)) {

     	    /* Handle as 8 bit value */
     	    if (FavourSize && val <= 2) {
     	    	AddCodeLine ("\tjsr\tpushc%d", (int) val);
     	    } else {
     	    	ldaconst (val);
     	    	AddCodeLine ("\tjsr\tpusha");
     	    }

     	} else {

     	    /* Handle as 16 bit value */
     	    hi = (unsigned char) (val >> 8);
     	    if (val <= 7) {
		AddCodeLine ("\tjsr\tpush%u", (unsigned) val);
     	    } else if (hi == 0 || hi == 0xFF) {
     	    	/* Use special function */
     	    	ldaconst (val);
       	       	AddCodeLine ("\tjsr\t%s", (hi == 0)? "pusha0" : "pushaFF");
     	    } else {
     	    	/* Long way ... */
     	    	g_getimmed (flags, val, 0);
     	    	AddCodeLine ("\tjsr\tpushax");
     	    }
     	}

    } else {

     	/* Value is not 16 bit or not constant */
     	if (flags & CF_CONST) {
     	    /* Constant 32 bit value, load into eax */
     	    g_getimmed (flags, val, 0);
     	}

     	/* Push the primary register */
     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    /* Handle as char */
     		    AddCodeLine ("\tjsr\tpusha");
     		    break;
     		}
     		/* FALL THROUGH */
     	    case CF_INT:
     		AddCodeLine ("\tjsr\tpushax");
     		break;

     	    case CF_LONG:
     	     	AddCodeLine ("\tjsr\tpusheax");
     		break;

     	    default:
     		typeerror (flags);

     	}

    }

    /* Adjust the stack offset */
    push (flags);
}



void g_swap (unsigned flags)
/* Swap the primary register and the top of the stack. flags give the type
 * of *both* values (must have same size).
 */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    AddCodeLine ("\tjsr\tswapstk");
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\tswapestk");
	    break;

	default:
	    typeerror (flags);

    }
}



void g_call (unsigned flags, char* lbl, unsigned argsize)
/* Call the specified subroutine name */
{
    if ((flags & CF_FIXARGC) == 0) {
	/* Pass arg count */
	ldyconst (argsize);
    }
    AddCodeLine ("\tjsr\t_%s", lbl);
    oursp += argsize;	    		/* callee pops args */
}



void g_callind (unsigned flags, unsigned argsize)
/* Call subroutine with address in AX */
{
    if ((flags & CF_FIXARGC) == 0) {
	/* Pass arg count */
	ldyconst (argsize);
    }
    AddCodeLine ("\tjsr\tcallax");	/* do the call */
    oursp += argsize;	    		/* callee pops args */
}



void g_jump (unsigned label)
/* Jump to specified internal label number */
{
    AddCodeLine ("\tjmp\tL%04X", label);
}



void g_switch (unsigned flags)
/* Output switch statement preample */
{
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	case CF_INT:
     	    AddCodeLine ("\tjsr\tswitch");
     	    break;

     	case CF_LONG:
     	    AddCodeLine ("\tjsr\tlswitch");
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_case (unsigned flags, unsigned label, unsigned long val)
/* Create table code for one case selector */
{
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
    	case CF_INT:
    	    AddCodeLine ("\t.word\t$%04X, L%04X", val & 0xFFFF, label & 0xFFFF);
       	    break;

    	case CF_LONG:
	    AddCodeLine ("\t.dword\t$%08X", val);
	    AddCodeLine ("\t.word\tL%04X", label & 0xFFFF);
    	    break;

    	default:
    	    typeerror (flags);

    }
}



void g_truejump (unsigned flags, unsigned label)
/* Jump to label if zero flag clear */
{
    if (flags & CF_SHORT) {
	AddCodeLine ("\tbne\tL%04X", label);
    } else {
        AddCodeLine ("\tjne\tL%04X", label);
    }
}



void g_falsejump (unsigned flags, unsigned label)
/* Jump to label if zero flag set */
{
    if (flags & CF_SHORT) {
    	AddCodeLine ("\tbeq\tL%04X", label);
    } else {
       	AddCodeLine ("\tjeq\tL%04X", label);
    }
}



static void mod_internal (int k, char* verb1, char* verb2)
{
    if (k <= 8) {
	AddCodeLine ("\tjsr\t%ssp%c", verb1, k + '0');
    } else {
	CheckLocalOffs (k);
	ldyconst (k);
	AddCodeLine ("\tjsr\t%ssp", verb2);
    }
}



void g_space (int space)
/* Create or drop space on the stack */
{
    if (space < 0) {
	mod_internal (-space, "inc", "addy");
    } else if (space > 0) {
	mod_internal (space, "dec", "suby");
    }
}



void g_add (unsigned flags, unsigned long val)
/* Primary = TOS + Primary */
{
    static char* ops [12] = {
     	0,		"tosadda0",	"tosaddax",
     	0,		"tosadda0",	"tosaddax",
     	0,		0,	 	"tosaddeax",
     	0,		0,	 	"tosaddeax",
    };

    if (flags & CF_CONST) {
    	flags &= ~CF_FORCECHAR;	// Handle chars as ints
     	g_push (flags & ~CF_CONST, 0);
    }
    oper (flags, val, ops);
}



void g_sub (unsigned flags, unsigned long val)
/* Primary = TOS - Primary */
{
    static char* ops [12] = {
     	0,		"tossuba0",	"tossubax",
     	0,		"tossuba0",	"tossubax",
     	0,		0,	 	"tossubeax",
     	0,		0,	 	"tossubeax",
    };

    if (flags & CF_CONST) {
    	flags &= ~CF_FORCECHAR;	// Handle chars as ints
     	g_push (flags & ~CF_CONST, 0);
    }
    oper (flags, val, ops);
}



void g_rsub (unsigned flags, unsigned long val)
/* Primary = Primary - TOS */
{
    static char* ops [12] = {
	0,		"tosrsuba0",	"tosrsubax",
	0,		"tosrsuba0",	"tosrsubax",
	0,		0,	 	"tosrsubeax",
	0,		0,	 	"tosrsubeax",
    };
    oper (flags, val, ops);
}



void g_mul (unsigned flags, unsigned long val)
/* Primary = TOS * Primary */
{
    static char* ops [12] = {
     	0,		"tosmula0",	"tosmulax",
     	0,   		"tosumula0",	"tosumulax",
     	0,		0,	 	"tosmuleax",
     	0,		0,	 	"tosumuleax",
    };

    int p2;

    /* Do strength reduction if the value is constant and a power of two */
    if (flags & CF_CONST && (p2 = powerof2 (val)) >= 0) {
     	/* Generate a shift instead */
     	g_asl (flags, p2);
	return;
    }

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
		    /* Handle some special cases */
		    switch (val) {

		     	case 3:
		     	    AddCodeLine ("\tsta\ttmp1");
		     	    AddCodeLine ("\tasl\ta");
		     	    AddCodeLine ("\tclc");
		     	    AddCodeLine ("\tadc\ttmp1");
		     	    return;

		     	case 5:
		     	    AddCodeLine ("\tsta\ttmp1");
		     	    AddCodeLine ("\tasl\ta");
		     	    AddCodeLine ("\tasl\ta");
		     	    AddCodeLine ("\tclc");
     		     	    AddCodeLine ("\tadc\ttmp1");
		     	    return;

		     	case 10:
		     	    AddCodeLine ("\tsta\ttmp1");
		     	    AddCodeLine ("\tasl\ta");
		     	    AddCodeLine ("\tasl\ta");
	     	     	    AddCodeLine ("\tclc");
		     	    AddCodeLine ("\tadc\ttmp1");
		     	    AddCodeLine ("\tasl\ta");
		     	    return;
		    }
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
    	flags &= ~CF_FORCECHAR;	// Handle chars as ints
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_div (unsigned flags, unsigned long val)
/* Primary = TOS / Primary */
{
    static char* ops [12] = {
     	0,		"tosdiva0",	"tosdivax",
     	0,		"tosudiva0",	"tosudivax",
	0,		0,  		"tosdiveax",
	0,		0,  		"tosudiveax",
    };

    /* Do strength reduction if the value is constant and a power of two */
    int p2;
    if ((flags & CF_CONST) && (p2 = powerof2 (val)) >= 0) {
	/* Generate a shift instead */
	g_asr (flags, p2);
    } else {
	/* Generate a division */
	if (flags & CF_CONST) {
	    /* lhs is not on stack */
    	    flags &= ~CF_FORCECHAR;	// Handle chars as ints
	    g_push (flags & ~CF_CONST, 0);
     	}
	oper (flags, val, ops);
    }
}



void g_mod (unsigned flags, unsigned long val)
/* Primary = TOS % Primary */
{
    static char* ops [12] = {
     	0,		"tosmoda0",	"tosmodax",
     	0,		"tosumoda0",	"tosumodax",
     	0,		0,  		"tosmodeax",
     	0,		0,  		"tosumodeax",
    };
    int p2;

    /* Check if we can do some cost reduction */
    if ((flags & CF_CONST) && (flags & CF_UNSIGNED) && val != 0xFFFFFFFF && (p2 = powerof2 (val)) >= 0) {
     	/* We can do that with an AND operation */
     	g_and (flags, val - 1);
    } else {
      	/* Do it the hard way... */
     	if (flags & CF_CONST) {
     	    /* lhs is not on stack */
    	    flags &= ~CF_FORCECHAR;	// Handle chars as ints
     	    g_push (flags & ~CF_CONST, 0);
     	}
      	oper (flags, val, ops);
    }
}



void g_or (unsigned flags, unsigned long val)
/* Primary = TOS | Primary */
{
    static char* ops [12] = {
      	0,  	     	"tosora0",	"tosorax",
      	0,  	     	"tosora0",	"tosorax",
      	0,  	     	0,  		"tosoreax",
      	0,  	     	0,     		"tosoreax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
     		    if ((val & 0xFF) != 0xFF) {
       	       	        AddCodeLine ("\tora\t#$%02X", val & 0xFF);
     		    }
      		    return;
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		if (val <= 0xFF) {
		    AddCodeLine ("\tora\t#$%02X", val & 0xFF);
		    return;
     		}
		break;

	    case CF_LONG:
		if (val <= 0xFF) {
		    AddCodeLine ("\tora\t#$%02X", val & 0xFF);
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_xor (unsigned flags, unsigned long val)
/* Primary = TOS ^ Primary */
{
    static char* ops [12] = {
	0,		"tosxora0",	"tosxorax",
	0,		"tosxora0",	"tosxorax",
	0,		0,	   	"tosxoreax",
	0,		0,	   	"tosxoreax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      		if (flags & CF_FORCECHAR) {
     		    if ((val & 0xFF) != 0) {
       	       	    	AddCodeLine ("\teor\t#$%02X", val & 0xFF);
     		    }
      		    return;
      		}
     		/* FALLTHROUGH */

	    case CF_INT:
		if (val <= 0xFF) {
		    if (val != 0) {
		     	AddCodeLine ("\teor\t#$%02X", val);
		    }
		    return;
		} else if ((val & 0xFF) == 0) {
		    AddCodeLine ("\tpha");
	     	    AddCodeLine ("\ttxa");
		    AddCodeLine ("\teor\t#$%02X", (val >> 8) & 0xFF);
		    AddCodeLine ("\ttax");
		    AddCodeLine ("\tpla");
		    return;
		}
		break;

	    case CF_LONG:
		if (val <= 0xFF) {
		    if (val != 0) {
       	       	       	AddCodeLine ("\teor\t#$%02X", val & 0xFF);
		    }
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_and (unsigned flags, unsigned long val)
/* Primary = TOS & Primary */
{
    static char* ops [12] = {
     	0,	     	"tosanda0",	"tosandax",
     	0,	     	"tosanda0",	"tosandax",
      	0,	     	0,		"tosandeax",
     	0,	     	0,		"tosandeax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    AddCodeLine ("\tand\t#$%02X", val & 0xFF);
     		    return;
     		}
     		/* FALLTHROUGH */
     	    case CF_INT:
		if ((val & 0xFFFF) != 0xFFFF) {
       	       	    if (val <= 0xFF) {
		    	ldxconst (0);
		    	if (val == 0) {
		    	    ldaconst (0);
		    	} else if (val != 0xFF) {
		       	    AddCodeLine ("\tand\t#$%02X", val & 0xFF);
		    	}
		    } else if ((val & 0xFF00) == 0xFF00) {
		    	AddCodeLine ("\tand\t#$%02X", val & 0xFF);
		    } else if ((val & 0x00FF) == 0x0000) {
			AddCodeLine ("\ttxa");
			AddCodeLine ("\tand\t#$%02X", (val >> 8) & 0xFF);
			AddCodeLine ("\ttax");
			ldaconst (0);
		    } else {
			AddCodeLine ("\ttay");
			AddCodeLine ("\ttxa");
			AddCodeLine ("\tand\t#$%02X", (val >> 8) & 0xFF);
			AddCodeLine ("\ttax");
			AddCodeLine ("\ttya");
			if ((val & 0x00FF) != 0x00FF) {
			    AddCodeLine ("\tand\t#$%02X", val & 0xFF);
			}
		    }
		}
		return;

	    case CF_LONG:
		if (val <= 0xFF) {
		    ldxconst (0);
		    AddCodeLine ("\tstx\tsreg+1");
	     	    AddCodeLine ("\tstx\tsreg");
		    if ((val & 0xFF) != 0xFF) {
		     	 AddCodeLine ("\tand\t#$%02X", val & 0xFF);
		    }
		    return;
		} else if (val == 0xFF00) {
		    ldaconst (0);
		    AddCodeLine ("\tsta\tsreg+1");
		    AddCodeLine ("\tsta\tsreg");
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_asr (unsigned flags, unsigned long val)
/* Primary = TOS >> Primary */
{
    static char* ops [12] = {
      	0,	     	"tosasra0",	"tosasrax",
      	0,	     	"tosshra0",	"tosshrax",
      	0,	     	0,		"tosasreax",
      	0,	     	0,		"tosshreax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      	    case CF_INT:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("\tjsr\tshrax%ld", val);
		    } else {
		       	AddCodeLine ("\tjsr\tasrax%ld", val);
		    }
		    return;
		} else if (val == 8 && (flags & CF_UNSIGNED)) {
      		    AddCodeLine ("\ttxa");
      		    ldxconst (0);
		    return;
		}
		break;

	    case CF_LONG:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("\tjsr\tshreax%ld", val);
		    } else {
		       	AddCodeLine ("\tjsr\tasreax%ld", val);
		    }
		    return;
		} else if (val == 8 && (flags & CF_UNSIGNED)) {
		    AddCodeLine ("\ttxa");
		    AddCodeLine ("\tldx\tsreg");
		    AddCodeLine ("\tldy\tsreg+1");
		    AddCodeLine ("\tsty\tsreg");
		    AddCodeLine ("\tldy\t#$00");
     		    AddCodeLine ("\tsty\tsreg+1");
		    return;
     		} else if (val == 16) {
		    AddCodeLine ("\tldy\t#$00");
		    AddCodeLine ("\tldx\tsreg+1");
		    if ((flags & CF_UNSIGNED) == 0) {
		        AddCodeLine ("\tbpl\t*+3");
		        AddCodeLine ("\tdey");
		        AddCodeHint ("y:!");
		    }
     		    AddCodeLine ("\tlda\tsreg");
		    AddCodeLine ("\tsty\tsreg+1");
		    AddCodeLine ("\tsty\tsreg");
	     	    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_asl (unsigned flags, unsigned long val)
/* Primary = TOS << Primary */
{
    static char* ops [12] = {
	0,	     	"tosasla0",    	"tosaslax",
	0,	     	"tosshla0",    	"tosshlax",
	0,	     	0,     	       	"tosasleax",
	0,	     	0,     	       	"tosshleax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
      	    case CF_INT:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("\tjsr\tshlax%ld", val);
		    } else {
	     	    	AddCodeLine ("\tjsr\taslax%ld", val);
		    }
		    return;
      		} else if (val == 8) {
      		    AddCodeLine ("\ttax");
      		    AddCodeLine ("\tlda\t#$00");
     		    return;
     		}
     		break;

	    case CF_LONG:
		if (val >= 1 && val <= 3) {
		    if (flags & CF_UNSIGNED) {
		       	AddCodeLine ("\tjsr\tshleax%ld", val);
		    } else {
		       	AddCodeLine ("\tjsr\tasleax%ld", val);
		    }
		    return;
		} else if (val == 8) {
		    AddCodeLine ("\tldy\tsreg");
		    AddCodeLine ("\tsty\tsreg+1");
		    AddCodeLine ("\tstx\tsreg");
		    AddCodeLine ("\ttax");
		    AddCodeLine ("\tlda\t#$00");
		    return;
		} else if (val == 16) {
		    AddCodeLine ("\tstx\tsreg+1");
		    AddCodeLine ("\tsta\tsreg");
		    AddCodeLine ("\tlda\t#$00");
		    AddCodeLine ("\ttax");
		    return;
		}
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_neg (unsigned flags)
/* Primary = -Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
     	case CF_INT:
	    AddCodeLine ("\tjsr\tnegax");
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\tnegeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_bneg (unsigned flags)
/* Primary = !Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	    AddCodeLine ("\tjsr\tbnega");
	    break;

	case CF_INT:
	    AddCodeLine ("\tjsr\tbnegax");
	    break;

	case CF_LONG:
     	    AddCodeLine ("\tjsr\tbnegeax");
	    break;

	default:
	    typeerror (flags);
    }
}



void g_com (unsigned flags)
/* Primary = ~Primary */
{
    switch (flags & CF_TYPE) {

	case CF_CHAR:
	case CF_INT:
	    AddCodeLine ("\tjsr\tcomplax");
	    break;

	case CF_LONG:
	    AddCodeLine ("\tjsr\tcompleax");
     	    break;

	default:
     	    typeerror (flags);
    }
}



void g_inc (unsigned flags, unsigned long val)
/* Increment the primary register by a given number */
{
    /* Don't inc by zero */
    if (val == 0) {
     	return;
    }

    /* Generate code for the supported types */
    flags &= ~CF_CONST;
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
     	    if (flags & CF_FORCECHAR) {
		if (CPU == CPU_65C02 && val <= 2) {
		    while (val--) {
		 	AddCodeLine ("\tina");
		    }
	     	} else {
		    AddCodeLine ("\tclc");
		    AddCodeLine ("\tadc\t#$%02X", val & 0xFF);
		}
     		break;
     	    }
     	    /* FALLTHROUGH */

     	case CF_INT:
	    if (CPU == CPU_65C02 && val == 1) {
		AddCodeLine ("\tina");
		AddCodeLine ("\tbne\t*+3");
		AddCodeLine ("\tinx");
		/* Tell the optimizer that the X register may be invalid */
		AddCodeHint ("x:!");
     	    } else if (FavourSize) {
     		/* Use jsr calls */
     		if (val <= 8) {
     		    AddCodeLine ("\tjsr\tincax%u", val);
     		} else if (val <= 255) {
     		    ldyconst (val);
     		    AddCodeLine ("\tjsr\tincaxy");
     		} else {
     		    g_add (flags | CF_CONST, val);
     		}
     	    } else {
     		/* Inline the code */
		if (val < 0x300) {
		    if ((val & 0xFF) != 0) {
		       	AddCodeLine ("\tclc");
		    	AddCodeLine ("\tadc\t#$%02X", (unsigned char) val);
		    	AddCodeLine ("\tbcc\t*+3");
		    	AddCodeLine ("\tinx");
		    	/* Tell the optimizer that the X register may be invalid */
       	       	       	AddCodeHint ("x:!");
		    }
     		    if (val >= 0x100) {
     		     	AddCodeLine ("\tinx");
     		    }
     		    if (val >= 0x200) {
     		    	AddCodeLine ("\tinx");
     		    }
     		} else {
		    AddCodeLine ("\tclc");
		    if ((val & 0xFF) != 0) {
	     	    	AddCodeLine ("\tadc\t#$%02X", (unsigned char) val);
			/* Tell the optimizer that the X register may be invalid */
			AddCodeHint ("x:!");
		    }
     		    AddCodeLine ("\tpha");
     		    AddCodeLine ("\ttxa");
     		    AddCodeLine ("\tadc\t#$%02X", (unsigned char) (val >> 8));
     		    AddCodeLine ("\ttax");
     		    AddCodeLine ("\tpla");
     		}
     	    }
     	    break;

       	case CF_LONG:
     	    if (val <= 255) {
     		ldyconst (val);
     		AddCodeLine ("\tjsr\tinceaxy");
     	    } else {
     		g_add (flags | CF_CONST, val);
     	    }
     	    break;

     	default:
     	    typeerror (flags);

    }
}



void g_dec (unsigned flags, unsigned long val)
/* Decrement the primary register by a given number */
{
    /* Generate code for the supported types */
    flags &= ~CF_CONST;
    switch (flags & CF_TYPE) {

     	case CF_CHAR:
	    if (flags & CF_FORCECHAR) {
		if (CPU == CPU_65C02 && val <= 2) {
		    while (val--) {
		 	AddCodeLine ("\tdea");
		    }
		} else {
		    AddCodeLine ("\tsec");
	     	    AddCodeLine ("\tsbc\t#$%02X", val & 0xFF);
		}
		break;
     	    }
	    /* FALLTHROUGH */

     	case CF_INT:
     	    if (val <= 2) {
     		AddCodeLine ("\tjsr\tdecax%d", (int) val);
     	    } else if (val <= 255) {
		ldyconst (val);
		AddCodeLine ("\tjsr\tdecaxy");
	    } else {
     		g_sub (flags | CF_CONST, val);
     	    }
     	    break;

     	case CF_LONG:
     	    if (val <= 255) {
     		ldyconst (val);
     		AddCodeLine ("\tjsr\tdeceaxy");
     	    } else {
     		g_sub (flags | CF_CONST, val);
	    }
	    break;

	default:
	    typeerror (flags);

    }
}



/*
 * Following are the conditional operators. They compare the TOS against
 * the primary and put a literal 1 in the primary if the condition is
 * true, otherwise they clear the primary register
 */



void g_eq (unsigned flags, unsigned long val)
/* Test for equal */
{
    static char* ops [12] = {
     	"toseq00",	"toseqa0",	"toseqax",
     	"toseq00",	"toseqa0",	"toseqax",
     	0,		0,		"toseqeax",
     	0,		0,		"toseqeax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    AddCodeLine ("\tjsr\tbooleq");
		    return;
		}
     		/* FALLTHROUGH */

      	    case CF_INT:
     		AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
       	       	AddCodeLine ("\tbne\t*+4");
     		AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
     		AddCodeLine ("\tjsr\tbooleq");
     		return;

     	    case CF_LONG:
     		break;

     	    default:
     		typeerror (flags);
     	}

     	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
     	 */
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_ne (unsigned flags, unsigned long val)
/* Test for not equal */
{
    static char* ops [12] = {
     	"tosne00",	"tosnea0",	"tosneax",
     	"tosne00",	"tosnea0",	"tosneax",
     	0,		0,		"tosneeax",
     	0,		0,		"tosneeax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

      	switch (flags & CF_TYPE) {

      	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
     		    AddCodeLine ("\tjsr\tboolne");
     		    return;
     		}
     		/* FALLTHROUGH */

      	    case CF_INT:
     		AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
     		AddCodeLine ("\tbne\t*+4");
     		AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
     		AddCodeLine ("\tjsr\tboolne");
     		return;

     	    case CF_LONG:
     		break;

     	    default:
     		typeerror (flags);
     	}

     	/* If we go here, we didn't emit code. Push the lhs on stack and fall
      	 * into the normal, non-optimized stuff.
     	 */
     	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_lt (unsigned flags, unsigned long val)
/* Test for less than */
{
    static char* ops [12] = {
     	"toslt00",	"toslta0", 	"tosltax",
     	"tosult00",	"tosulta0",	"tosultax",
     	0,		0,    	   	"toslteax",
     	0,		0,    	   	"tosulteax",
    };

    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Give a warning in some special cases */
     	if ((flags & CF_UNSIGNED) && val == 0) {
     	    Warning (WARN_COND_NEVER_TRUE);
     	}

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

     	    case CF_CHAR:
     		if (flags & CF_FORCECHAR) {
     		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
     		    if (flags & CF_UNSIGNED) {
     			AddCodeLine ("\tjsr\tboolult");
     		    } else {
     		        AddCodeLine ("\tjsr\tboollt");
     		    }
     		    return;
     		}
     	     	/* FALLTHROUGH */

     	    case CF_INT:
		if ((flags & CF_UNSIGNED) == 0 && val == 0) {
		    /* If we have a signed compare against zero, we only need to
		     * test the high byte.
		     */
		    AddCodeLine ("\ttxa");
		    AddCodeLine ("\tjsr\tboollt");
		    return;
		}
		/* Direct code only for unsigned data types */
		if (flags & CF_UNSIGNED) {
		    AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
       	       	    AddCodeLine ("\tbne\t*+4");
     	 	    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
	 	    AddCodeLine ("\tjsr\tboolult");
	 	    return;
     	 	}
     	 	break;

     	    case CF_LONG:
     	 	break;

     	    default:
	 	typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_le (unsigned flags, unsigned long val)
/* Test for less than or equal to */
{
    static char* ops [12] = {
	"tosle00",   	"toslea0",	"tosleax",
	"tosule00",  	"tosulea0",	"tosuleax",
	0,	     	0,    		"tosleeax",
	0,	     	0,    		"tosuleeax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    if (flags & CF_UNSIGNED) {
		     	AddCodeLine ("\tjsr\tboolule");
		    } else {
		        AddCodeLine ("\tjsr\tboolle");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if (flags & CF_UNSIGNED) {
		    AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
       	       	    AddCodeLine ("\tbne\t*+4");
     		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    AddCodeLine ("\tjsr\tboolule");
		    return;
		}
	  	break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_gt (unsigned flags, unsigned long val)
/* Test for greater than */
{
    static char* ops [12] = {
	"tosgt00",    	"tosgta0",	"tosgtax",
	"tosugt00",   	"tosugta0",	"tosugtax",
	0,	      	0,	    	"tosgteax",
	0,	      	0, 	    	"tosugteax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

     	/* Look at the type */
     	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    if (flags & CF_UNSIGNED) {
		      	/* If we have a compare > 0, we will replace it by
		      	 * != 0 here, since both are identical but the latter
		      	 * is easier to optimize.
		      	 */
		      	if (val & 0xFF) {
		       	    AddCodeLine ("\tjsr\tboolugt");
		      	} else {
		      	    AddCodeLine ("\tjsr\tboolne");
		      	}
		    } else {
	     	        AddCodeLine ("\tjsr\tboolgt");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if (flags & CF_UNSIGNED) {
		    /* If we have a compare > 0, we will replace it by
		     * != 0 here, since both are identical but the latter
		     * is easier to optimize.
		     */
		    if ((val & 0xFFFF) == 0) {
			AddCodeLine ("\tstx\ttmp1");
			AddCodeLine ("\tora\ttmp1");
			AddCodeLine ("\tjsr\tboolne");
		    } else {
       	       	       	AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
			AddCodeLine ("\tbne\t*+4");
			AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
       	       	       	AddCodeLine ("\tjsr\tboolugt");
		    }
		    return;
       	       	}
		break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



void g_ge (unsigned flags, unsigned long val)
/* Test for greater than or equal to */
{
    static char* ops [12] = {
     	"tosge00",	"tosgea0",  	"tosgeax",
     	"tosuge00",	"tosugea0",	"tosugeax",
     	0,		0,		"tosgeeax",
     	0,		0,		"tosugeeax",
    };


    /* If the right hand side is const, the lhs is not on stack but still
     * in the primary register.
     */
    if (flags & CF_CONST) {

	/* Give a warning in some special cases */
	if ((flags & CF_UNSIGNED) && val == 0) {
     	    Warning (WARN_COND_ALWAYS_TRUE);
	}

	/* Look at the type */
	switch (flags & CF_TYPE) {

	    case CF_CHAR:
		if (flags & CF_FORCECHAR) {
		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    if (flags & CF_UNSIGNED) {
			AddCodeLine ("\tjsr\tbooluge");
		    } else {
		        AddCodeLine ("\tjsr\tboolge");
		    }
		    return;
		}
		/* FALLTHROUGH */

	    case CF_INT:
		if (flags & CF_UNSIGNED) {
       	       	    AddCodeLine ("\tcpx\t#$%02X", (val >> 8) & 0xFF);
       	       	    AddCodeLine ("\tbne\t*+4");
     		    AddCodeLine ("\tcmp\t#$%02X", val & 0xFF);
		    AddCodeLine ("\tjsr\tbooluge");
		    return;
		}
	     	break;

	    case CF_LONG:
		break;

	    default:
		typeerror (flags);
	}

	/* If we go here, we didn't emit code. Push the lhs on stack and fall
	 * into the normal, non-optimized stuff.
	 */
	g_push (flags & ~CF_CONST, 0);

    }

    /* Use long way over the stack */
    oper (flags, val, ops);
}



/*****************************************************************************/
/*   			   Allocating static storage	     		     */
/*****************************************************************************/



void g_res (unsigned n)
/* Reserve static storage, n bytes */
{
    AddCodeLine ("\t.res\t%u,$00", n);
}



void g_defdata (unsigned flags, unsigned long val, unsigned offs)
/* Define data with the size given in flags */
{
    if (flags & CF_CONST) {

	/* Numeric constant */
	switch (flags & CF_TYPE) {

	    case CF_CHAR:
	     	AddCodeLine ("\t.byte\t$%02lX", val & 0xFF);
		break;

	    case CF_INT:
		AddCodeLine ("\t.word\t$%04lX", val & 0xFFFF);
		break;

	    case CF_LONG:
		AddCodeLine ("\t.dword\t$%08lX", val & 0xFFFFFFFF);
		break;

	    default:
		typeerror (flags);
		break;

	}

    } else {

	/* Create the correct label name */
	const char* Label = GetLabelName (flags, val, offs);

	/* Labels are always 16 bit */
	AddCodeLine ("\t.word\t%s", Label);

    }
}



void g_defbytes (const unsigned char* Bytes, unsigned Count)
/* Output a row of bytes as a constant */
{
    unsigned Chunk;
    char Buf [128];
    char* B;

    /* Output the stuff */
    while (Count) {

     	/* How many go into this line? */
     	if ((Chunk = Count) > 16) {
     	    Chunk = 16;
     	}
     	Count -= Chunk;

     	/* Output one line */
	strcpy (Buf, "\t.byte\t");
       	B = Buf + 7;
     	do {
	    B += sprintf (B, "$%02X", *Bytes++ & 0xFF);
     	    if (--Chunk) {
		*B++ = ',';
     	    }
     	} while (Chunk);

	/* Output the line */
     	AddCodeLine (Buf);
    }
}



void g_zerobytes (unsigned n)
/* Output n bytes of data initialized with zero */
{
    AddCodeLine ("\t.res\t%u,$00", n);
}



/*****************************************************************************/
/*	     		    Inlined known functions			     */
/*****************************************************************************/



void g_strlen (unsigned flags, unsigned long val, unsigned offs)
/* Inline the strlen() function */
{
    /* We need a label in both cases */
    unsigned label = GetLabel ();

    /* Two different encodings */
    if (flags & CF_CONST) {

	/* The address of the string is constant. Create the correct label name */
    	char* lbuf = GetLabelName (flags, val, offs);

	/* Generate the strlen code */
	AddCodeLine ("\tldy\t#$FF");
	g_defloclabel (label);
	AddCodeLine ("\tiny");
	AddCodeLine ("\tlda\t%s,y", lbuf);
	AddCodeLine ("\tbne\tL%04X", label);
       	AddCodeLine ("\ttax");
	AddCodeLine ("\ttya");

    } else {

       	/* Address not constant but in primary */
	if (FavourSize) {
	    /* This is too much code, so call strlen instead of inlining */
    	    AddCodeLine ("\tjsr\t_strlen");
	} else {
	    /* Inline the function */
	    AddCodeLine ("\tsta\tptr1");
	    AddCodeLine ("\tstx\tptr1+1");
	    AddCodeLine ("\tldy\t#$FF");
	    g_defloclabel (label);
	    AddCodeLine ("\tiny");
	    AddCodeLine ("\tlda\t(ptr1),y");
	    AddCodeLine ("\tbne\tL%04X", label);
       	    AddCodeLine ("\ttax");
	    AddCodeLine ("\ttya");
     	}
    }
}



