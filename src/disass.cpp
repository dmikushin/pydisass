/* objdump.c -- dump information about an object file.
   Copyright (C) 1990-2024 Free Software Foundation, Inc.

   This file is part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */


/* Objdump overview.

   Objdump displays information about one or more object files, either on
   their own, or inside libraries.  It is commonly used as a disassembler,
   but it can also display information about file headers, symbol tables,
   relocations, debugging directives and more.

   The flow of execution is as follows:

   1. Command line arguments are checked for control switches and the
      information to be displayed is selected.

   2. Any remaining arguments are assumed to be object files, and they are
      processed in order by display_bfd().  If the file is an archive each
      of its elements is processed in turn.

   3. The file's target architecture and binary file format are determined
      by bfd_check_format().  If they are recognised, then dump_bfd() is
      called.

   4. dump_bfd() in turn calls separate functions to display the requested
      item(s) of information(s).  For example disassemble_data() is called if
      a disassembly has been requested.

   When disassembling the code loops through blocks of instructions bounded
   by symbols, calling disassemble_bytes() on each block.  The actual
   disassembling is done by the libopcodes library, via a function pointer
   supplied by the disassembler() function.  */

extern "C"
{

#include "sysdep.h"
#include "bfd.h"
#include "elf-bfd.h"
#include "coff-bfd.h"
#include "bucomm.h"
#include "elfcomm.h"
#include "demanguse.h"
#include "getopt.h"
#include "safe-ctype.h"
#include "dis-asm.h"
#include "libiberty.h"
#include "demangle.h"
#include "filenames.h"
#include "debug.h"
#include "budbg.h"
#include "objdump.h"

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

/* Internal headers for the ELF .stab-dump code - sorry.  */
#define	BYTES_IN_WORD	32
#include "aout/aout64.h"

} // extern "C"

#include "disass/disass.h"
#include "streamprintf.h"

#include <iostream>
#include <sstream>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using namespace disass;

static std::stringstream ssdisass;

/* Exit status.  */
static int exit_status = 0;

/* The following variables are set based on arguments passed on the
   command line.  */
static int dump_reloc_info;		/* -r */
static int dump_dynamic_reloc_info;	/* -R */
static int no_addresses;		/* --no-addresses */
static int prefix_addresses;		/* --prefix-addresses */
static int with_line_numbers;		/* -l */
static bool with_source_code;		/* -S */
static int show_raw_insn;		/* --show-raw-insn */
static int do_demangle;			/* -C, --demangle */
static bool disassemble;		/* -d */
static bool disassemble_all;		/* -D */
static int disassemble_zeroes;		/* --disassemble-zeroes */
int wide_output;			/* -w */
static int insn_width;			/* --insn-width */
static bfd_vma start_address = (bfd_vma) -1; /* --start-address */
static bfd_vma stop_address = (bfd_vma) -1;  /* --stop-address */
static int file_start_context = 0;      /* --file-start-context */
static bool display_file_offsets;	/* -F */
static const char *prefix;		/* --prefix */
static int prefix_strip;		/* --prefix-strip */
static size_t prefix_length;
static bool unwind_inlines;		/* --inlines.  */
static const char * disasm_sym;		/* Disassembly start symbol.  */
static const char * source_comment;     /* --source_comment.  */
static bool visualize_jumps = false;	/* --visualize-jumps.  */
static bool color_output = false;	/* --visualize-jumps=color.  */
static bool extended_color_output = false; /* --visualize-jumps=extended-color.  */
static int show_all_symbols;            /* --show-all-symbols.  */

static enum color_selection
  {
    on_if_terminal_output,
    on,   				/* --disassembler-color=color.  */
    off, 				/* --disassembler-color=off.  */
    extended				/* --disassembler-color=extended-color.  */
  } disassembler_color =
#if DEFAULT_FOR_COLORED_DISASSEMBLY
  on_if_terminal_output;
#else
  off;
#endif

static int demangle_flags = DMGL_ANSI | DMGL_PARAMS;

/* This is reset to false each time we enter the disassembler, and set true
   when the disassembler emits something in the dis_style_comment_start
   style.  Once this is true, all further output on that line is done in
   the comment style.  This only has an effect when disassembler coloring
   is turned on.  */
static bool disassembler_in_comment = false;

/* A structure to record the sections mentioned in -j switches.  */
struct only
{
  const char *name; /* The name of the section.  */
  bool seen; /* A flag to indicate that the section has been found in one or more input files.  */
  struct only *next; /* Pointer to the next structure in the list.  */
};
/* Pointer to an array of 'only' structures.
   This pointer is NULL if the -j switch has not been used.  */
static struct only * only_list = NULL;

/* Variables for handling include file path table.  */
static const char **include_paths;
static int include_path_count;

/* Extra info to pass to the section disassembler and address printing
   function.  */
struct objdump_disasm_info
{
  bfd *abfd;
  bool require_sec;
  disassembler_ftype disassemble_fn;
  arelent *reloc;
  const char *symbol;
};

/* Architecture to disassemble for, or default if NULL.  */
static const char *machine = NULL;

/* Target specific options to the disassembler.  */
static char *disassembler_options = NULL;

/* Endianness to disassemble for, or default if BFD_ENDIAN_UNKNOWN.  */
static enum bfd_endian endian = BFD_ENDIAN_UNKNOWN;

/* The symbol table.  */
static asymbol **syms;

/* The sorted symbol table.  */
static asymbol **sorted_syms;

/* Number of symbols in `sorted_syms'.  */
static long sorted_symcount = 0;

/* The dynamic symbol table.  */
static asymbol **dynsyms;

/* Handlers for -P/--private.  */
static const struct objdump_private_desc * const objdump_private_vectors[] =
  {
    OBJDUMP_PRIVATE_VECTORS
    NULL
  };

/* The list of detected jumps inside a function.  */
static struct jump_info *detected_jumps = NULL;

typedef enum unicode_display_type
{
  unicode_default = 0,
  unicode_locale,
  unicode_escape,
  unicode_hex,
  unicode_highlight,
  unicode_invalid
} unicode_display_type;

static unicode_display_type unicode_display = unicode_default;

static void
my_bfd_nonfatal (const char *msg)
{
  bfd_nonfatal (msg);
  exit_status = 1;
}

/* Convert a potential UTF-8 encoded sequence in IN into characters in OUT.
   The conversion format is controlled by the unicode_display variable.
   Returns the number of characters added to OUT.
   Returns the number of bytes consumed from IN in CONSUMED.
   Always consumes at least one byte and displays at least one character.  */

static unsigned int
display_utf8 (const unsigned char * in, char * out, unsigned int * consumed)
{
  char *        orig_out = out;
  unsigned int  nchars = 0;
  unsigned int j;

  if (unicode_display == unicode_default)
    goto invalid;

  if (in[0] < 0xc0)
    goto invalid;

  if ((in[1] & 0xc0) != 0x80)
    goto invalid;

  if ((in[0] & 0x20) == 0)
    {
      nchars = 2;
      goto valid;
    }

  if ((in[2] & 0xc0) != 0x80)
    goto invalid;

  if ((in[0] & 0x10) == 0)
    {
      nchars = 3;
      goto valid;
    }

  if ((in[3] & 0xc0) != 0x80)
    goto invalid;

  nchars = 4;

 valid:
  switch (unicode_display)
    {
    case unicode_locale:
      /* Copy the bytes into the output buffer as is.  */
      memcpy (out, in, nchars);
      out += nchars;
      break;

    case unicode_invalid:
    case unicode_hex:
      *out++ = unicode_display == unicode_hex ? '<' : '{';
      *out++ = '0';
      *out++ = 'x';
      for (j = 0; j < nchars; j++)
	out += sprintf (out, "%02x", in [j]);
      *out++ = unicode_display == unicode_hex ? '>' : '}';
      break;

    case unicode_highlight:
      if (isatty (1))
	out += sprintf (out, "\x1B[31;47m"); /* Red.  */
      /* Fall through.  */
    case unicode_escape:
      switch (nchars)
	{
	case 2:
	  out += sprintf (out, "\\u%02x%02x",
		  ((in[0] & 0x1c) >> 2),
		  ((in[0] & 0x03) << 6) | (in[1] & 0x3f));
	  break;

	case 3:
	  out += sprintf (out, "\\u%02x%02x",
		  ((in[0] & 0x0f) << 4) | ((in[1] & 0x3c) >> 2),
		  ((in[1] & 0x03) << 6) | ((in[2] & 0x3f)));
	  break;

	case 4:
	  out += sprintf (out, "\\u%02x%02x%02x",
		  ((in[0] & 0x07) << 6) | ((in[1] & 0x3c) >> 2),
		  ((in[1] & 0x03) << 6) | ((in[2] & 0x3c) >> 2),
		  ((in[2] & 0x03) << 6) | ((in[3] & 0x3f)));
	  break;
	default:
	  /* URG.  */
	  break;
	}

      if (unicode_display == unicode_highlight && isatty (1))
	out += sprintf (out, "\x1B[0m"); /* Default colour.  */
      break;

    default:
      /* URG */
      break;
    }

  * consumed = nchars;
  return out - orig_out;

 invalid:
  /* Not a valid UTF-8 sequence.  */
  *out = *in;
  * consumed = 1;
  return 1;
}

/* Returns a version of IN with any control characters
   replaced by escape sequences.  Uses a static buffer
   if necessary.

   If unicode display is enabled, then also handles the
   conversion of unicode characters.  */

static const char *
sanitize_string (const char * in)
{
  static char *  buffer = NULL;
  static size_t  buffer_len = 0;
  const char *   original = in;
  char *         out;

  /* Paranoia.  */
  if (in == NULL)
    return "";

  /* See if any conversion is necessary.  In the majority
     of cases it will not be needed.  */
  do
    {
      unsigned char c = *in++;

      if (c == 0)
	return original;

      if (ISCNTRL (c))
	break;

      if (unicode_display != unicode_default && c >= 0xc0)
	break;
    }
  while (1);

  /* Copy the input, translating as needed.  */
  in = original;
  /* For 2 char unicode, max out is 12 (colour escapes) + 6, ie. 9 per in
     For hex, max out is 8 for 2 char unicode, ie. 4 per in.
     3 and 4 char unicode produce less output for input.  */
  size_t max_needed = strlen (in) * 9 + 1;
  if (buffer_len < max_needed)
    {
      buffer_len = max_needed;
      free (buffer);
      buffer = (char*)xmalloc (buffer_len);
    }

  out = buffer;
  do
    {
      unsigned char c = *in++;

      if (c == 0)
	break;

      if (ISCNTRL (c))
	{
	  *out++ = '^';
	  *out++ = c + 0x40;
	}
      else if (unicode_display != unicode_default && c >= 0xc0)
	{
	  unsigned int num_consumed;

	  out += display_utf8 ((const unsigned char *) --in, out, &num_consumed);
	  in += num_consumed;
	}
      else
	*out++ = c;
    }
  while (1);

  *out = 0;
  return buffer;
}

/* Returns TRUE if the specified section should be dumped.  */

static bool
process_section_p (asection * section)
{
  struct only * only;

  if (only_list == NULL)
    return true;

  for (only = only_list; only; only = only->next)
    if (strcmp (only->name, section->name) == 0)
      {
	only->seen = true;
	return true;
      }

  return false;
}

static const asection *compare_section;

/* Sort symbols into value order.  */

static int
compare_symbols (const void *ap, const void *bp)
{
  const asymbol *a = * (const asymbol **) ap;
  const asymbol *b = * (const asymbol **) bp;
  const char *an;
  const char *bn;
  size_t anl;
  size_t bnl;
  bool as, af, bs, bf;
  flagword aflags;
  flagword bflags;

  if (bfd_asymbol_value (a) > bfd_asymbol_value (b))
    return 1;
  else if (bfd_asymbol_value (a) < bfd_asymbol_value (b))
    return -1;

  /* Prefer symbols from the section currently being disassembled.
     Don't sort symbols from other sections by section, since there
     isn't much reason to prefer one section over another otherwise.
     See sym_ok comment for why we compare by section name.  */
  as = strcmp (compare_section->name, a->section->name) == 0;
  bs = strcmp (compare_section->name, b->section->name) == 0;
  if (as && !bs)
    return -1;
  if (!as && bs)
    return 1;

  an = bfd_asymbol_name (a);
  bn = bfd_asymbol_name (b);
  anl = strlen (an);
  bnl = strlen (bn);

  /* The symbols gnu_compiled and gcc2_compiled convey no real
     information, so put them after other symbols with the same value.  */
  af = (strstr (an, "gnu_compiled") != NULL
	|| strstr (an, "gcc2_compiled") != NULL);
  bf = (strstr (bn, "gnu_compiled") != NULL
	|| strstr (bn, "gcc2_compiled") != NULL);

  if (af && ! bf)
    return 1;
  if (! af && bf)
    return -1;

  /* We use a heuristic for the file name, to try to sort it after
     more useful symbols.  It may not work on non Unix systems, but it
     doesn't really matter; the only difference is precisely which
     symbol names get printed.  */

#define file_symbol(s, sn, snl)			\
  (((s)->flags & BSF_FILE) != 0			\
   || ((snl) > 2				\
       && (sn)[(snl) - 2] == '.'		\
       && ((sn)[(snl) - 1] == 'o'		\
	   || (sn)[(snl) - 1] == 'a')))

  af = file_symbol (a, an, anl);
  bf = file_symbol (b, bn, bnl);

  if (af && ! bf)
    return 1;
  if (! af && bf)
    return -1;

  /* Sort function and object symbols before global symbols before
     local symbols before section symbols before debugging symbols.  */

  aflags = a->flags;
  bflags = b->flags;

  if ((aflags & BSF_DEBUGGING) != (bflags & BSF_DEBUGGING))
    {
      if ((aflags & BSF_DEBUGGING) != 0)
	return 1;
      else
	return -1;
    }
  if ((aflags & BSF_SECTION_SYM) != (bflags & BSF_SECTION_SYM))
    {
      if ((aflags & BSF_SECTION_SYM) != 0)
	return 1;
      else
	return -1;
    }
  if ((aflags & BSF_FUNCTION) != (bflags & BSF_FUNCTION))
    {
      if ((aflags & BSF_FUNCTION) != 0)
	return -1;
      else
	return 1;
    }
  if ((aflags & BSF_OBJECT) != (bflags & BSF_OBJECT))
    {
      if ((aflags & BSF_OBJECT) != 0)
	return -1;
      else
	return 1;
    }
  if ((aflags & BSF_LOCAL) != (bflags & BSF_LOCAL))
    {
      if ((aflags & BSF_LOCAL) != 0)
	return 1;
      else
	return -1;
    }
  if ((aflags & BSF_GLOBAL) != (bflags & BSF_GLOBAL))
    {
      if ((aflags & BSF_GLOBAL) != 0)
	return -1;
      else
	return 1;
    }

  /* Sort larger size ELF symbols before smaller.  See PR20337.  */
  bfd_vma asz = 0;
  if ((a->flags & (BSF_SECTION_SYM | BSF_SYNTHETIC)) == 0
      && bfd_get_flavour (bfd_asymbol_bfd (a)) == bfd_target_elf_flavour)
    asz = ((elf_symbol_type *) a)->internal_elf_sym.st_size;
  bfd_vma bsz = 0;
  if ((b->flags & (BSF_SECTION_SYM | BSF_SYNTHETIC)) == 0
      && bfd_get_flavour (bfd_asymbol_bfd (b)) == bfd_target_elf_flavour)
    bsz = ((elf_symbol_type *) b)->internal_elf_sym.st_size;
  if (asz != bsz)
    return asz > bsz ? -1 : 1;

  /* Symbols that start with '.' might be section names, so sort them
     after symbols that don't start with '.'.  */
  if (an[0] == '.' && bn[0] != '.')
    return 1;
  if (an[0] != '.' && bn[0] == '.')
    return -1;

  /* Finally, if we can't distinguish them in any other way, try to
     get consistent results by sorting the symbols by name.  */
  return strcmp (an, bn);
}

/* Sort relocs into address order.  */

static int
compare_relocs (const void *ap, const void *bp)
{
  const arelent *a = * (const arelent **) ap;
  const arelent *b = * (const arelent **) bp;

  if (a->address > b->address)
    return 1;
  else if (a->address < b->address)
    return -1;

  /* So that associated relocations tied to the same address show up
     in the correct order, we don't do any further sorting.  */
  if (a > b)
    return 1;
  else if (a < b)
    return -1;
  else
    return 0;
}

/* Print an address (VMA) to the output stream in INFO.
   If SKIP_ZEROES is TRUE, omit leading zeroes.  */

static void
objdump_print_value (bfd_vma vma, struct disassemble_info *inf,
		     bool skip_zeroes)
{
  char buf[30];
  char *p;
  struct objdump_disasm_info *aux;

  aux = (struct objdump_disasm_info *) inf->application_data;
  bfd_sprintf_vma (aux->abfd, buf, vma);
  if (! skip_zeroes)
    p = buf;
  else
    {
      for (p = buf; *p == '0'; ++p)
	;
      if (*p == '\0')
	--p;
    }
  (*inf->fprintf_styled_func) (inf->stream, dis_style_address, "%s", p);
}

/* Print the name of a symbol.  */

static void
objdump_print_symname (bfd *abfd, struct disassemble_info *inf,
		       asymbol *sym)
{
  char *alloc;
  const char *name, *version_string = NULL;
  bool hidden = false;

  alloc = NULL;
  name = bfd_asymbol_name (sym);
  if (do_demangle && name[0] != '\0')
    {
      /* Demangle the name.  */
      alloc = bfd_demangle (abfd, name, demangle_flags);
      if (alloc != NULL)
	name = alloc;
    }

  if ((sym->flags & (BSF_SECTION_SYM | BSF_SYNTHETIC)) == 0)
    version_string = bfd_get_symbol_version_string (abfd, sym, true,
						    &hidden);

  if (bfd_is_und_section (bfd_asymbol_section (sym)))
    hidden = true;

  name = sanitize_string (name);

  if (inf != NULL)
    {
      (*inf->fprintf_styled_func) (inf->stream, dis_style_symbol, "%s", name);
      if (version_string && *version_string != '\0')
	(*inf->fprintf_styled_func) (inf->stream, dis_style_symbol,
				     hidden ? "@%s" : "@@%s",
				     version_string);
    }
  else
    {
      printf ("%s", name);
      if (version_string && *version_string != '\0')
	printf (hidden ? "@%s" : "@@%s", version_string);
    }

  if (alloc != NULL)
    free (alloc);
}

static inline bool
sym_ok (bool want_section,
	bfd *abfd ATTRIBUTE_UNUSED,
	long place,
	asection *sec,
	struct disassemble_info *inf)
{
  if (want_section)
    {
      /* NB: An object file can have different sections with the same
	 section name.  Compare compare section pointers if they have
	 the same owner.  */
      if (sorted_syms[place]->section->owner == sec->owner
	  && sorted_syms[place]->section != sec)
	return false;

      /* Note - we cannot just compare section pointers because they could
	 be different, but the same...  Ie the symbol that we are trying to
	 find could have come from a separate debug info file.  Under such
	 circumstances the symbol will be associated with a section in the
	 debug info file, whilst the section we want is in a normal file.
	 So the section pointers will be different, but the section names
	 will be the same.  */
      if (strcmp (bfd_section_name (sorted_syms[place]->section),
		  bfd_section_name (sec)) != 0)
	return false;
    }

  return inf->symbol_is_valid (sorted_syms[place], inf);
}

/* Locate a symbol given a bfd and a section (from INFO->application_data),
   and a VMA.  If INFO->application_data->require_sec is TRUE, then always
   require the symbol to be in the section.  Returns NULL if there is no
   suitable symbol.  If PLACE is not NULL, then *PLACE is set to the index
   of the symbol in sorted_syms.  */

static asymbol *
find_symbol_for_address (bfd_vma vma,
			 struct disassemble_info *inf,
			 long *place)
{
  /* @@ Would it speed things up to cache the last two symbols returned,
     and maybe their address ranges?  For many processors, only one memory
     operand can be present at a time, so the 2-entry cache wouldn't be
     constantly churned by code doing heavy memory accesses.  */

  /* Indices in `sorted_syms'.  */
  long min = 0;
  long max_count = sorted_symcount;
  long thisplace;
  struct objdump_disasm_info *aux;
  bfd *abfd;
  asection *sec;
  unsigned int opb;
  bool want_section;
  long rel_count;

  if (sorted_symcount < 1)
    return NULL;

  aux = (struct objdump_disasm_info *) inf->application_data;
  abfd = aux->abfd;
  sec = inf->section;
  opb = inf->octets_per_byte;

  /* Perform a binary search looking for the closest symbol to the
     required value.  We are searching the range (min, max_count].  */
  while (min + 1 < max_count)
    {
      asymbol *sym;

      thisplace = (max_count + min) / 2;
      sym = sorted_syms[thisplace];

      if (bfd_asymbol_value (sym) > vma)
	max_count = thisplace;
      else if (bfd_asymbol_value (sym) < vma)
	min = thisplace;
      else
	{
	  min = thisplace;
	  break;
	}
    }

  /* The symbol we want is now in min, the low end of the range we
     were searching.  If there are several symbols with the same
     value, we want the first one.  */
  thisplace = min;
  while (thisplace > 0
	 && (bfd_asymbol_value (sorted_syms[thisplace])
	     == bfd_asymbol_value (sorted_syms[thisplace - 1])))
    --thisplace;

  /* Prefer a symbol in the current section if we have multple symbols
     with the same value, as can occur with overlays or zero size
     sections.  */
  min = thisplace;
  while (min < max_count
	 && (bfd_asymbol_value (sorted_syms[min])
	     == bfd_asymbol_value (sorted_syms[thisplace])))
    {
      if (sym_ok (true, abfd, min, sec, inf))
	{
	  thisplace = min;

	  if (place != NULL)
	    *place = thisplace;

	  return sorted_syms[thisplace];
	}
      ++min;
    }

  /* If the file is relocatable, and the symbol could be from this
     section, prefer a symbol from this section over symbols from
     others, even if the other symbol's value might be closer.

     Note that this may be wrong for some symbol references if the
     sections have overlapping memory ranges, but in that case there's
     no way to tell what's desired without looking at the relocation
     table.

     Also give the target a chance to reject symbols.  */
  want_section = (aux->require_sec
		  || ((abfd->flags & HAS_RELOC) != 0
		      && vma >= bfd_section_vma (sec)
		      && vma < (bfd_section_vma (sec)
				+ bfd_section_size (sec) / opb)));

  if (! sym_ok (want_section, abfd, thisplace, sec, inf))
    {
      long i;
      long newplace = sorted_symcount;

      for (i = min - 1; i >= 0; i--)
	{
	  if (sym_ok (want_section, abfd, i, sec, inf))
	    {
	      if (newplace == sorted_symcount)
		newplace = i;

	      if (bfd_asymbol_value (sorted_syms[i])
		  != bfd_asymbol_value (sorted_syms[newplace]))
		break;

	      /* Remember this symbol and keep searching until we reach
		 an earlier address.  */
	      newplace = i;
	    }
	}

      if (newplace != sorted_symcount)
	thisplace = newplace;
      else
	{
	  /* We didn't find a good symbol with a smaller value.
	     Look for one with a larger value.  */
	  for (i = thisplace + 1; i < sorted_symcount; i++)
	    {
	      if (sym_ok (want_section, abfd, i, sec, inf))
		{
		  thisplace = i;
		  break;
		}
	    }
	}

      if (! sym_ok (want_section, abfd, thisplace, sec, inf))
	/* There is no suitable symbol.  */
	return NULL;
    }

  /* If we have not found an exact match for the specified address
     and we have dynamic relocations available, then we can produce
     a better result by matching a relocation to the address and
     using the symbol associated with that relocation.  */
  rel_count = inf->dynrelcount;
  if (!want_section
      && sorted_syms[thisplace]->value != vma
      && rel_count > 0
      && inf->dynrelbuf != NULL
      && inf->dynrelbuf[0]->address <= vma
      && inf->dynrelbuf[rel_count - 1]->address >= vma
      /* If we have matched a synthetic symbol, then stick with that.  */
      && (sorted_syms[thisplace]->flags & BSF_SYNTHETIC) == 0)
    {
      arelent **  rel_low;
      arelent **  rel_high;

      rel_low = inf->dynrelbuf;
      rel_high = rel_low + rel_count - 1;
      while (rel_low <= rel_high)
	{
	  arelent **rel_mid = &rel_low[(rel_high - rel_low) / 2];
	  arelent * rel = *rel_mid;

	  if (rel->address == vma)
	    {
	      /* Absolute relocations do not provide a more helpful
		 symbolic address.  Find a non-absolute relocation
		 with the same address.  */
	      arelent **rel_vma = rel_mid;
	      for (rel_mid--;
		   rel_mid >= rel_low && rel_mid[0]->address == vma;
		   rel_mid--)
		rel_vma = rel_mid;

	      for (; rel_vma <= rel_high && rel_vma[0]->address == vma;
		   rel_vma++)
		{
		  rel = *rel_vma;
		  if (rel->sym_ptr_ptr != NULL
		      && ! bfd_is_abs_section ((* rel->sym_ptr_ptr)->section))
		    {
		      if (place != NULL)
			* place = thisplace;
		      return * rel->sym_ptr_ptr;
		    }
		}
	      break;
	    }

	  if (vma < rel->address)
	    rel_high = rel_mid;
	  else if (vma >= rel_mid[1]->address)
	    rel_low = rel_mid + 1;
	  else
	    break;
	}
    }

  if (place != NULL)
    *place = thisplace;

  return sorted_syms[thisplace];
}

/* Print an address and the offset to the nearest symbol.  */

static void
objdump_print_addr_with_sym (bfd *abfd, asection *sec, asymbol *sym,
			     bfd_vma vma, struct disassemble_info *inf,
			     bool skip_zeroes)
{
  if (!no_addresses)
    {
      objdump_print_value (vma, inf, skip_zeroes);
      (*inf->fprintf_styled_func) (inf->stream, dis_style_text, " ");
    }

  if (sym == NULL)
    {
      bfd_vma secaddr;

      (*inf->fprintf_styled_func) (inf->stream, dis_style_text,"<");
      (*inf->fprintf_styled_func) (inf->stream, dis_style_symbol, "%s",
				   sanitize_string (bfd_section_name (sec)));
      secaddr = bfd_section_vma (sec);
      if (vma < secaddr)
	{
	  (*inf->fprintf_styled_func) (inf->stream, dis_style_immediate,
				       "-0x");
	  objdump_print_value (secaddr - vma, inf, true);
	}
      else if (vma > secaddr)
	{
	  (*inf->fprintf_styled_func) (inf->stream, dis_style_immediate, "+0x");
	  objdump_print_value (vma - secaddr, inf, true);
	}
      (*inf->fprintf_styled_func) (inf->stream, dis_style_text, ">");
    }
  else
    {
      (*inf->fprintf_styled_func) (inf->stream, dis_style_text, "<");

      objdump_print_symname (abfd, inf, sym);

      if (bfd_asymbol_value (sym) == vma)
	;
      /* Undefined symbols in an executables and dynamic objects do not have
	 a value associated with them, so it does not make sense to display
	 an offset relative to them.  Normally we would not be provided with
	 this kind of symbol, but the target backend might choose to do so,
	 and the code in find_symbol_for_address might return an as yet
	 unresolved symbol associated with a dynamic reloc.  */
      else if ((bfd_get_file_flags (abfd) & (EXEC_P | DYNAMIC))
	       && bfd_is_und_section (sym->section))
	;
      else if (bfd_asymbol_value (sym) > vma)
	{
	  (*inf->fprintf_styled_func) (inf->stream, dis_style_immediate,"-0x");
	  objdump_print_value (bfd_asymbol_value (sym) - vma, inf, true);
	}
      else if (vma > bfd_asymbol_value (sym))
	{
	  (*inf->fprintf_styled_func) (inf->stream, dis_style_immediate, "+0x");
	  objdump_print_value (vma - bfd_asymbol_value (sym), inf, true);
	}

      (*inf->fprintf_styled_func) (inf->stream, dis_style_text, ">");
    }

  if (display_file_offsets)
    inf->fprintf_styled_func (inf->stream, dis_style_text,
			      _(" (File Offset: 0x%lx)"),
			      (long int)(sec->filepos + (vma - sec->vma)));
}

/* Displays all symbols in the sorted symbol table starting at PLACE
   which match the address VMA.  Assumes that show_all_symbols == true.  */

static void
display_extra_syms (long place,
		    bfd_vma vma,
		    struct disassemble_info *inf)
{
  struct objdump_disasm_info *aux = (struct objdump_disasm_info *) inf->application_data;

  if (place == 0)
    return;

  bool first = true;

  for (; place < sorted_symcount; place++)
    {
      asymbol *sym = sorted_syms[place];
		  
      if (bfd_asymbol_value (sym) != vma)
	break;

      if (! inf->symbol_is_valid (sym, inf))
	continue;

      if (first)
	inf->fprintf_styled_func (inf->stream, dis_style_immediate, ",\n\t<");
      else  
	inf->fprintf_styled_func (inf->stream, dis_style_immediate, ", <");

      objdump_print_symname (aux->abfd, inf, sym);
      inf->fprintf_styled_func (inf->stream, dis_style_immediate, ">");
      first = false;
    }
}
		    
/* Print an address (VMA), symbolically if possible.
   If SKIP_ZEROES is TRUE, don't output leading zeroes.  */

static void
objdump_print_addr (bfd_vma vma,
		    struct disassemble_info *inf,
		    bool skip_zeroes)
{
  struct objdump_disasm_info *aux;
  asymbol *sym = NULL;
  bool skip_find = false;
  long place = 0;

  aux = (struct objdump_disasm_info *) inf->application_data;

  if (sorted_symcount < 1)
    {
      if (!no_addresses)
	{
	  (*inf->fprintf_styled_func) (inf->stream, dis_style_address, "0x");
	  objdump_print_value (vma, inf, skip_zeroes);
	}

      if (display_file_offsets)
	inf->fprintf_styled_func (inf->stream, dis_style_text,
				  _(" (File Offset: 0x%lx)"),
				  (long int) (inf->section->filepos
					      + (vma - inf->section->vma)));
      return;
    }

  if (aux->reloc != NULL
      && aux->reloc->sym_ptr_ptr != NULL
      && * aux->reloc->sym_ptr_ptr != NULL)
    {
      sym = * aux->reloc->sym_ptr_ptr;

      /* Adjust the vma to the reloc.  */
      vma += bfd_asymbol_value (sym);

      if (bfd_is_und_section (bfd_asymbol_section (sym)))
	skip_find = true;
    }

  if (!skip_find)
    sym = find_symbol_for_address (vma, inf, &place);

  objdump_print_addr_with_sym (aux->abfd, inf->section, sym, vma, inf,
			       skip_zeroes);

  /* If requested, display any extra symbols at this address.  */
  if (sym == NULL || ! show_all_symbols)
    return;

  if (place)
    display_extra_syms (place + 1, vma, inf);
    
  /* If we found an absolute symbol in the reloc (ie: "*ABS*+0x....")
     and there is a valid symbol at the address contained in the absolute symbol
     then display any extra symbols that match this address.  This helps
     particularly with relocations for PLT entries.  */
  if (startswith (sym->name, BFD_ABS_SECTION_NAME "+"))
    {
      bfd_vma addr = strtoul (sym->name + strlen (BFD_ABS_SECTION_NAME "+"), NULL, 0);

      if (addr && addr != vma)
	{
	  sym = find_symbol_for_address (addr, inf, &place);

	  if (sym)
	    display_extra_syms (place, addr, inf);
	}
    }
}

/* Print VMA to INFO.  This function is passed to the disassembler
   routine.  */

static void
objdump_print_address (bfd_vma vma, struct disassemble_info *inf)
{
  objdump_print_addr (vma, inf, ! prefix_addresses);
}

/* Determine if the given address has a symbol associated with it.  */

static asymbol *
objdump_symbol_at_address (bfd_vma vma, struct disassemble_info * inf)
{
  asymbol * sym;

  sym = find_symbol_for_address (vma, inf, NULL);
  if (sym != NULL && bfd_asymbol_value (sym) == vma)
    return sym;

  return NULL;
}

/* Hold the last function name and the last line number we displayed
   in a disassembly.  */

static char *prev_functionname;
static unsigned int prev_line;
static unsigned int prev_discriminator;

/* We keep a list of all files that we have seen when doing a
   disassembly with source, so that we know how much of the file to
   display.  This can be important for inlined functions.  */

struct print_file_list
{
  struct print_file_list *next;
  const char *filename;
  const char *modname;
  const char *map;
  size_t mapsize;
  const char **linemap;
  unsigned maxline;
  unsigned last_line;
  unsigned max_printed;
  int first;
};

static struct print_file_list *print_files;

/* The number of preceding context lines to show when we start
   displaying a file for the first time.  */

#define SHOW_PRECEDING_CONTEXT_LINES (5)

/* Reads the contents of file FN into memory.  Returns a pointer to the buffer.
   Also returns the size of the buffer in SIZE_RETURN and a filled out
   stat structure in FST_RETURN.  Returns NULL upon failure.  */

static const char *
slurp_file (const char *   fn,
	    size_t *       size_return,
	    struct stat *  fst_return,
	    bfd *          abfd ATTRIBUTE_UNUSED)
{
#ifdef HAVE_MMAP
  int ps;
  size_t msize;
#endif
  const char *map;
  int fd;

  /* Paranoia.  */
  if (fn == NULL || * fn == 0 || size_return == NULL || fst_return == NULL)
    return NULL;

  fd = open (fn, O_RDONLY | O_BINARY);

  if (fd < 0)
    return NULL;

  if (fstat (fd, fst_return) < 0)
    {
      close (fd);
      return NULL;
    }

  *size_return = fst_return->st_size;

#ifdef HAVE_MMAP
  ps = getpagesize ();
  msize = (*size_return + ps - 1) & ~(ps - 1);
  map = (const char *) mmap (NULL, msize, PROT_READ, MAP_SHARED, fd, 0);
  if (map != (char *) -1L)
    {
      close (fd);
      return map;
    }
#endif

  map = (const char *) malloc (*size_return);
  if (!map || (size_t) read (fd, (char *) map, *size_return) != *size_return)
    {
      free ((void *) map);
      map = NULL;
    }
  close (fd);
  return map;
}

#define line_map_decrease 5

/* Precompute array of lines for a mapped file. */

static const char **
index_file (const char *map, size_t size, unsigned int *maxline)
{
  const char *p, *lstart, *end;
  int chars_per_line = 45; /* First iteration will use 40.  */
  unsigned int lineno;
  const char **linemap = NULL;
  unsigned long line_map_size = 0;

  lineno = 0;
  lstart = map;
  end = map + size;

  for (p = map; p < end; p++)
    {
      if (*p == '\n')
	{
	  if (p + 1 < end && p[1] == '\r')
	    p++;
	}
      else if (*p == '\r')
	{
	  if (p + 1 < end && p[1] == '\n')
	    p++;
	}
      else
	continue;

      /* End of line found.  */

      if (linemap == NULL || line_map_size < lineno + 1)
	{
	  unsigned long newsize;

	  chars_per_line -= line_map_decrease;
	  if (chars_per_line <= 1)
	    chars_per_line = 1;
	  line_map_size = size / chars_per_line + 1;
	  if (line_map_size < lineno + 1)
	    line_map_size = lineno + 1;
	  newsize = line_map_size * sizeof (char *);
	  linemap = (const char **) xrealloc (linemap, newsize);
	}

      linemap[lineno++] = lstart;
      lstart = p + 1;
    }

  *maxline = lineno;
  return linemap;
}

/* Tries to open MODNAME, and if successful adds a node to print_files
   linked list and returns that node.  Also fills in the stat structure
   pointed to by FST_RETURN.  Returns NULL on failure.  */

static struct print_file_list *
try_print_file_open (const char *   origname,
		     const char *   modname,
		     struct stat *  fst_return,
		     bfd *          abfd)
{
  struct print_file_list *p;

  p = (struct print_file_list *) xmalloc (sizeof (struct print_file_list));

  p->map = slurp_file (modname, &p->mapsize, fst_return, abfd);
  if (p->map == NULL)
    {
      free (p);
      return NULL;
    }

  p->linemap = index_file (p->map, p->mapsize, &p->maxline);
  p->last_line = 0;
  p->max_printed = 0;
  p->filename = origname;
  p->modname = modname;
  p->next = print_files;
  p->first = 1;
  print_files = p;
  return p;
}

/* If the source file, as described in the symtab, is not found
   try to locate it in one of the paths specified with -I
   If found, add location to print_files linked list.  */

static struct print_file_list *
update_source_path (const char *filename, bfd *abfd)
{
  struct print_file_list *p;
  const char *fname;
  struct stat fst;
  int i;

  p = try_print_file_open (filename, filename, &fst, abfd);
  if (p == NULL)
    {
      if (include_path_count == 0)
	return NULL;

      /* Get the name of the file.  */
      fname = lbasename (filename);

      /* If file exists under a new path, we need to add it to the list
	 so that show_line knows about it.  */
      for (i = 0; i < include_path_count; i++)
	{
	  char *modname = concat (include_paths[i], "/", fname,
				  (const char *) 0);

	  p = try_print_file_open (filename, modname, &fst, abfd);
	  if (p)
	    break;

	  free (modname);
	}
    }

  if (p != NULL)
    {
      long mtime = bfd_get_mtime (abfd);

      if (fst.st_mtime > mtime)
	warn (_("source file %s is more recent than object file\n"),
	      filename);
    }

  return p;
}

/* Print a source file line.  */

static void
print_line (struct print_file_list *p, unsigned int linenum)
{
  const char *l;
  size_t len;

  if (linenum >= p->maxline)
    return;
  l = p->linemap [linenum];
  if (source_comment != NULL && strlen (l) > 0)
    printf ("%s", source_comment);
  len = strcspn (l, "\n\r");
  /* Test fwrite return value to quiet glibc warning.  */
  if (len == 0 || fwrite (l, len, 1, stdout) == 1)
    putchar ('\n');
}

/* Print a range of source code lines. */

static void
dump_lines (struct print_file_list *p, unsigned int start, unsigned int end)
{
  if (p->map == NULL)
    return;
  if (start != 0)
    --start;
  while (start < end)
    {
      print_line (p, start);
      start++;
    }
}

/* Show the line number, or the source line, in a disassembly
   listing.  */

static void
show_line (bfd *abfd, asection *section, bfd_vma addr_offset)
{
  const char *filename;
  const char *functionname;
  unsigned int linenumber;
  unsigned int discriminator;
  bool reloc;
  char *path = NULL;

  if (! with_line_numbers && ! with_source_code)
    return;

#ifdef HAVE_LIBDEBUGINFOD
  {
    bfd *debug_bfd;
    const char *alt_filename = NULL;

    if (use_debuginfod)
      {
	bfd *alt_bfd;

	/* PR 29075: Check for separate debuginfo and .gnu_debugaltlink files.
	   They need to be passed to bfd_find_nearest_line_with_alt in case they
	   were downloaded from debuginfod.  Otherwise libbfd will attempt to
	   search for them and fail to locate them.  */
	debug_bfd = find_separate_debug (abfd);
	if (debug_bfd == NULL)
	  debug_bfd = abfd;

	alt_bfd = find_alt_debug (debug_bfd);
	if (alt_bfd != NULL)
	  alt_filename = bfd_get_filename (alt_bfd);
      }
    else
      debug_bfd = abfd;

    bfd_set_error (bfd_error_no_error);
    if (! bfd_find_nearest_line_with_alt (debug_bfd, alt_filename,
					  section, syms,
					  addr_offset, &filename,
					  &functionname, &linenumber,
					  &discriminator))
      {
	if (bfd_get_error () == bfd_error_no_error)
	  return;
	if (! bfd_find_nearest_line_discriminator (abfd, section, syms,
						   addr_offset, &filename,
						   &functionname, &linenumber,
						   &discriminator))
	  return;
      }
  }
#else
  if (! bfd_find_nearest_line_discriminator (abfd, section, syms, addr_offset,
					     &filename, &functionname,
					     &linenumber, &discriminator))
    return;
#endif

  if (filename != NULL && *filename == '\0')
    filename = NULL;
  if (functionname != NULL && *functionname == '\0')
    functionname = NULL;

  if (filename
      && IS_ABSOLUTE_PATH (filename)
      && prefix)
    {
      char *path_up;
      const char *fname = filename;

      path = (char*) xmalloc (prefix_length + 1 + strlen (filename));

      if (prefix_length)
	memcpy (path, prefix, prefix_length);
      path_up = path + prefix_length;

      /* Build relocated filename, stripping off leading directories
	 from the initial filename if requested.  */
      if (prefix_strip > 0)
	{
	  int level = 0;
	  const char *s;

	  /* Skip selected directory levels.  */
	  for (s = fname + 1; *s != '\0' && level < prefix_strip; s++)
	    if (IS_DIR_SEPARATOR (*s))
	      {
		fname = s;
		level++;
	      }
	}

      /* Update complete filename.  */
      strcpy (path_up, fname);

      filename = path;
      reloc = true;
    }
  else
    reloc = false;

  if (with_line_numbers)
    {
      if (functionname != NULL
	  && (prev_functionname == NULL
	      || strcmp (functionname, prev_functionname) != 0))
	{
	  char *demangle_alloc = NULL;
	  if (do_demangle && functionname[0] != '\0')
	    {
	      /* Demangle the name.  */
	      demangle_alloc = bfd_demangle (abfd, functionname,
					     demangle_flags);
	    }

	  /* Demangling adds trailing parens, so don't print those.  */
	  if (demangle_alloc != NULL)
	    printf ("%s:\n", sanitize_string (demangle_alloc));
	  else
	    printf ("%s():\n", sanitize_string (functionname));

	  prev_line = -1;
	  free (demangle_alloc);
	}
      if (linenumber > 0
	  && (linenumber != prev_line
	      || discriminator != prev_discriminator))
	{
	  if (discriminator > 0)
	    printf ("%s:%u (discriminator %u)\n",
		    filename == NULL ? "???" : sanitize_string (filename),
		    linenumber, discriminator);
	  else
	    printf ("%s:%u\n", filename == NULL
		    ? "???" : sanitize_string (filename),
		    linenumber);
	}
      if (unwind_inlines)
	{
	  const char *filename2;
	  const char *functionname2;
	  unsigned line2;

	  while (bfd_find_inliner_info (abfd, &filename2, &functionname2,
					&line2))
	    {
	      printf ("inlined by %s:%u",
		      sanitize_string (filename2), line2);
	      printf (" (%s)\n", sanitize_string (functionname2));
	    }
	}
    }

  if (with_source_code
      && filename != NULL
      && linenumber > 0)
    {
      struct print_file_list **pp, *p;
      unsigned l;

      for (pp = &print_files; *pp != NULL; pp = &(*pp)->next)
	if (filename_cmp ((*pp)->filename, filename) == 0)
	  break;
      p = *pp;

      if (p == NULL)
	{
	  if (reloc)
	    filename = xstrdup (filename);
	  p = update_source_path (filename, abfd);
	}

      if (p != NULL && linenumber != p->last_line)
	{
	  if (file_start_context && p->first)
	    l = 1;
	  else
	    {
	      l = linenumber - SHOW_PRECEDING_CONTEXT_LINES;
	      if (l >= linenumber)
		l = 1;
	      if (p->max_printed >= l)
		{
		  if (p->max_printed < linenumber)
		    l = p->max_printed + 1;
		  else
		    l = linenumber;
		}
	    }
	  dump_lines (p, l, linenumber);
	  if (p->max_printed < linenumber)
	    p->max_printed = linenumber;
	  p->last_line = linenumber;
	  p->first = 0;
	}
    }

  if (functionname != NULL
      && (prev_functionname == NULL
	  || strcmp (functionname, prev_functionname) != 0))
    {
      if (prev_functionname != NULL)
	free (prev_functionname);
      prev_functionname = (char *) xmalloc (strlen (functionname) + 1);
      strcpy (prev_functionname, functionname);
    }

  if (linenumber > 0 && linenumber != prev_line)
    prev_line = linenumber;

  if (discriminator != prev_discriminator)
    prev_discriminator = discriminator;

  if (path)
    free (path);
}

/* Pseudo FILE object for strings.  */
typedef struct
{
  char *buffer;
  size_t pos;
  size_t alloc;
} SFILE;

/* sprintf to a "stream".  */

static int ATTRIBUTE_PRINTF_2
objdump_sprintf (SFILE *f, const char *format, ...)
{
  size_t n;
  va_list args;

  while (1)
    {
      size_t space = f->alloc - f->pos;

      va_start (args, format);
      n = vsnprintf (f->buffer + f->pos, space, format, args);
      va_end (args);

      if (space > n)
	break;

      f->alloc = (f->alloc + n) * 2;
      f->buffer = (char *) xrealloc (f->buffer, f->alloc);
    }
  f->pos += n;

  return n;
}

/* Return an integer greater than, or equal to zero, representing the color
   for STYLE, or -1 if no color should be used.  */

static int
objdump_color_for_disassembler_style (enum disassembler_style style)
{
  int color = -1;

  if (style == dis_style_comment_start)
    disassembler_in_comment = true;

  if (disassembler_color == on)
    {
      if (disassembler_in_comment)
	return color;

      switch (style)
	{
	case dis_style_symbol:
	  color = 32;
	  break;
        case dis_style_assembler_directive:
	case dis_style_sub_mnemonic:
	case dis_style_mnemonic:
	  color = 33;
	  break;
	case dis_style_register:
	  color = 34;
	  break;
	case dis_style_address:
        case dis_style_address_offset:
	case dis_style_immediate:
	  color = 35;
	  break;
	default:
	case dis_style_text:
	  color = -1;
	  break;
	}
    }
  else if (disassembler_color == extended)
    {
      if (disassembler_in_comment)
	return 250;

      switch (style)
	{
	case dis_style_symbol:
	  color = 40;
	  break;
        case dis_style_assembler_directive:
	case dis_style_sub_mnemonic:
	case dis_style_mnemonic:
	  color = 142;
	  break;
	case dis_style_register:
	  color = 27;
	  break;
	case dis_style_address:
        case dis_style_address_offset:
	case dis_style_immediate:
	  color = 134;
	  break;
	default:
	case dis_style_text:
	  color = -1;
	  break;
	}
    }
  else if (disassembler_color != off)
    bfd_fatal (_("disassembly color not correctly selected"));

  return color;
}

/* Like objdump_sprintf, but add in escape sequences to highlight the
   content according to STYLE.  */

static int ATTRIBUTE_PRINTF_3
objdump_styled_sprintf (SFILE *f, enum disassembler_style style,
			const char *format, ...)
{
  size_t n;
  va_list args;
  int color = objdump_color_for_disassembler_style (style);

  if (color >= 0)
    {
      while (1)
	{
	  size_t space = f->alloc - f->pos;

	  if (disassembler_color == on)
	    n = snprintf (f->buffer + f->pos, space, "\033[%dm", color);
	  else
	    n = snprintf (f->buffer + f->pos, space, "\033[38;5;%dm", color);
	  if (space > n)
	    break;

	  f->alloc = (f->alloc + n) * 2;
	  f->buffer = (char *) xrealloc (f->buffer, f->alloc);
	}
      f->pos += n;
    }

  while (1)
    {
      size_t space = f->alloc - f->pos;

      va_start (args, format);
      n = vsnprintf (f->buffer + f->pos, space, format, args);
      va_end (args);

      if (space > n)
	break;

      f->alloc = (f->alloc + n) * 2;
      f->buffer = (char *) xrealloc (f->buffer, f->alloc);
    }
  f->pos += n;

  if (color >= 0)
    {
      while (1)
	{
	  size_t space = f->alloc - f->pos;

	  n = snprintf (f->buffer + f->pos, space, "\033[0m");

	  if (space > n)
	    break;

	  f->alloc = (f->alloc + n) * 2;
	  f->buffer = (char *) xrealloc (f->buffer, f->alloc);
	}
      f->pos += n;
    }

  return n;
}

/* We discard the styling information here.  This function is only used
   when objdump is printing auxiliary information, the symbol headers, and
   disassembly address, or the bytes of the disassembled instruction.  We
   don't (currently) apply styling to any of this stuff, so, for now, just
   print the content with no additional style added.  */

static int ATTRIBUTE_PRINTF_3
fprintf_styled (FILE *f, enum disassembler_style style ATTRIBUTE_UNUSED,
		const char *fmt, ...)
{
  int res;
  va_list ap;

  va_start (ap, fmt);
  res = vfprintf (f, fmt, ap);
  va_end (ap);

  return res;
}

/* Code for generating (colored) diagrams of control flow start and end
   points.  */

/* Structure used to store the properties of a jump.  */

struct jump_info
{
  /* The next jump, or NULL if this is the last object.  */
  struct jump_info *next;
  /* The previous jump, or NULL if this is the first object.  */
  struct jump_info *prev;
  /* The start addresses of the jump.  */
  struct
    {
      /* The list of start addresses.  */
      bfd_vma *addresses;
      /* The number of elements.  */
      size_t count;
      /* The maximum number of elements that fit into the array.  */
      size_t max_count;
    } start;
  /* The end address of the jump.  */
  bfd_vma end;
  /* The drawing level of the jump.  */
  int level;
};

/* Construct a jump object for a jump from start
   to end with the corresponding level.  */

static struct jump_info *
jump_info_new (bfd_vma start, bfd_vma end, int level)
{
  struct jump_info *result = (struct jump_info *) xmalloc (sizeof (struct jump_info));

  result->next = NULL;
  result->prev = NULL;
  result->start.addresses = (bfd_vma *) xmalloc (sizeof (bfd_vma *) * 2);
  result->start.addresses[0] = start;
  result->start.count = 1;
  result->start.max_count = 2;
  result->end = end;
  result->level = level;

  return result;
}

/* Free a jump object and return the next object
   or NULL if this was the last one.  */

static struct jump_info *
jump_info_free (struct jump_info *ji)
{
  struct jump_info *result = NULL;

  if (ji)
    {
      result = ji->next;
      if (ji->start.addresses)
	free (ji->start.addresses);
      free (ji);
    }

  return result;
}

/* Get the smallest value of all start and end addresses.  */

static bfd_vma
jump_info_min_address (const struct jump_info *ji)
{
  bfd_vma min_address = ji->end;
  size_t i;

  for (i = ji->start.count; i-- > 0;)
    if (ji->start.addresses[i] < min_address)
      min_address = ji->start.addresses[i];
  return min_address;
}

/* Get the largest value of all start and end addresses.  */

static bfd_vma
jump_info_max_address (const struct jump_info *ji)
{
  bfd_vma max_address = ji->end;
  size_t i;

  for (i = ji->start.count; i-- > 0;)
    if (ji->start.addresses[i] > max_address)
      max_address = ji->start.addresses[i];
  return max_address;
}

/* Get the target address of a jump.  */

static bfd_vma
jump_info_end_address (const struct jump_info *ji)
{
  return ji->end;
}

/* Test if an address is one of the start addresses of a jump.  */

static bool
jump_info_is_start_address (const struct jump_info *ji, bfd_vma address)
{
  bool result = false;
  size_t i;

  for (i = ji->start.count; i-- > 0;)
    if (address == ji->start.addresses[i])
      {
	result = true;
	break;
      }

  return result;
}

/* Test if an address is the target address of a jump.  */

static bool
jump_info_is_end_address (const struct jump_info *ji, bfd_vma address)
{
  return (address == ji->end);
}

/* Get the difference between the smallest and largest address of a jump.  */

static bfd_vma
jump_info_size (const struct jump_info *ji)
{
  return jump_info_max_address (ji) - jump_info_min_address (ji);
}

/* Unlink a jump object from a list.  */

static void
jump_info_unlink (struct jump_info *node,
		  struct jump_info **base)
{
  if (node->next)
    node->next->prev = node->prev;
  if (node->prev)
    node->prev->next = node->next;
  else
    *base = node->next;
  node->next = NULL;
  node->prev = NULL;
}

/* Insert unlinked jump info node into a list.  */

static void
jump_info_insert (struct jump_info *node,
		  struct jump_info *target,
		  struct jump_info **base)
{
  node->next = target;
  node->prev = target->prev;
  target->prev = node;
  if (node->prev)
    node->prev->next = node;
  else
    *base = node;
}

/* Add unlinked node to the front of a list.  */

static void
jump_info_add_front (struct jump_info *node,
		     struct jump_info **base)
{
  node->next = *base;
  if (node->next)
    node->next->prev = node;
  node->prev = NULL;
  *base = node;
}

/* Move linked node to target position.  */

static void
jump_info_move_linked (struct jump_info *node,
		       struct jump_info *target,
		       struct jump_info **base)
{
  /* Unlink node.  */
  jump_info_unlink (node, base);
  /* Insert node at target position.  */
  jump_info_insert (node, target, base);
}

/* Test if two jumps intersect.  */

static bool
jump_info_intersect (const struct jump_info *a,
		     const struct jump_info *b)
{
  return ((jump_info_max_address (a) >= jump_info_min_address (b))
	  && (jump_info_min_address (a) <= jump_info_max_address (b)));
}

/* Merge two compatible jump info objects.  */

static void
jump_info_merge (struct jump_info **base)
{
  struct jump_info *a;

  for (a = *base; a; a = a->next)
    {
      struct jump_info *b;

      for (b = a->next; b; b = b->next)
	{
	  /* Merge both jumps into one.  */
	  if (a->end == b->end)
	    {
	      /* Reallocate addresses.  */
	      size_t needed_size = a->start.count + b->start.count;
	      size_t i;

	      if (needed_size > a->start.max_count)
		{
		  a->start.max_count += b->start.max_count;
		  a->start.addresses =
		    (bfd_vma*) xrealloc (a->start.addresses,
			      a->start.max_count * sizeof (bfd_vma *));
		}

	      /* Append start addresses.  */
	      for (i = 0; i < b->start.count; ++i)
		a->start.addresses[a->start.count++] =
		  b->start.addresses[i];

	      /* Remove and delete jump.  */
	      struct jump_info *tmp = b->prev;
	      jump_info_unlink (b, base);
	      jump_info_free (b);
	      b = tmp;
	    }
	}
    }
}

/* Sort jumps by their size and starting point using a stable
   minsort. This could be improved if sorting performance is
   an issue, for example by using mergesort.  */

static void
jump_info_sort (struct jump_info **base)
{
  struct jump_info *current_element = *base;

  while (current_element)
    {
      struct jump_info *best_match = current_element;
      struct jump_info *runner = current_element->next;
      bfd_vma best_size = jump_info_size (best_match);

      while (runner)
	{
	  bfd_vma runner_size = jump_info_size (runner);

	  if ((runner_size < best_size)
	      || ((runner_size == best_size)
		  && (jump_info_min_address (runner)
		      < jump_info_min_address (best_match))))
	    {
	      best_match = runner;
	      best_size = runner_size;
	    }

	  runner = runner->next;
	}

      if (best_match == current_element)
	current_element = current_element->next;
      else
	jump_info_move_linked (best_match, current_element, base);
    }
}

/* Visualize all jumps at a given address.  */

static void
jump_info_visualize_address (bfd_vma address,
			     int max_level,
			     char *line_buffer,
			     uint8_t *color_buffer)
{
  struct jump_info *ji = detected_jumps;
  size_t len = (max_level + 1) * 3;

  /* Clear line buffer.  */
  memset (line_buffer, ' ', len);
  memset (color_buffer, 0, len);

  /* Iterate over jumps and add their ASCII art.  */
  while (ji)
    {
      /* Discard jumps that are never needed again.  */
      if (jump_info_max_address (ji) < address)
	{
	  struct jump_info *tmp = ji;

	  ji = ji->next;
	  jump_info_unlink (tmp, &detected_jumps);
	  jump_info_free (tmp);
	  continue;
	}

      /* This jump intersects with the current address.  */
      if (jump_info_min_address (ji) <= address)
	{
	  /* Hash target address to get an even
	     distribution between all values.  */
	  bfd_vma hash_address = jump_info_end_address (ji);
	  uint8_t color = iterative_hash_object (hash_address, 0);
	  /* Fetch line offset.  */
	  int offset = (max_level - ji->level) * 3;

	  /* Draw start line.  */
	  if (jump_info_is_start_address (ji, address))
	    {
	      size_t i = offset + 1;

	      for (; i < len - 1; ++i)
		if (line_buffer[i] == ' ')
		  {
		    line_buffer[i] = '-';
		    color_buffer[i] = color;
		  }

	      if (line_buffer[i] == ' ')
		{
		  line_buffer[i] = '-';
		  color_buffer[i] = color;
		}
	      else if (line_buffer[i] == '>')
		{
		  line_buffer[i] = 'X';
		  color_buffer[i] = color;
		}

	      if (line_buffer[offset] == ' ')
		{
		  if (address <= ji->end)
		    line_buffer[offset] =
		      (jump_info_min_address (ji) == address) ? ',': '+';
		  else
		    line_buffer[offset] =
		      (jump_info_max_address (ji) == address) ? '\'': '+';
		  color_buffer[offset] = color;
		}
	    }
	  /* Draw jump target.  */
	  else if (jump_info_is_end_address (ji, address))
	    {
	      size_t i = offset + 1;

	      for (; i < len - 1; ++i)
		if (line_buffer[i] == ' ')
		  {
		    line_buffer[i] = '-';
		    color_buffer[i] = color;
		  }

	      if (line_buffer[i] == ' ')
		{
		  line_buffer[i] = '>';
		  color_buffer[i] = color;
		}
	      else if (line_buffer[i] == '-')
		{
		  line_buffer[i] = 'X';
		  color_buffer[i] = color;
		}

	      if (line_buffer[offset] == ' ')
		{
		  if (jump_info_min_address (ji) < address)
		    line_buffer[offset] =
		      (jump_info_max_address (ji) > address) ? '>' : '\'';
		  else
		    line_buffer[offset] = ',';
		  color_buffer[offset] = color;
		}
	    }
	  /* Draw intermediate line segment.  */
	  else if (line_buffer[offset] == ' ')
	    {
	      line_buffer[offset] = '|';
	      color_buffer[offset] = color;
	    }
	}

      ji = ji->next;
    }
}

/* Clone of disassemble_bytes to detect jumps inside a function.  */
/* FIXME: is this correct? Can we strip it down even further?  */

static struct jump_info *
disassemble_jumps (struct disassemble_info * inf,
		   disassembler_ftype        disassemble_fn,
		   bfd_vma                   start_offset,
		   bfd_vma                   stop_offset,
		   bfd_vma		     rel_offset,
		   arelent **                relpp,
		   arelent **                relppend)
{
  struct objdump_disasm_info *aux;
  struct jump_info *jumps = NULL;
  asection *section;
  bfd_vma addr_offset;
  unsigned int opb = inf->octets_per_byte;
  int octets = opb;
  SFILE sfile;

  aux = (struct objdump_disasm_info *) inf->application_data;
  section = inf->section;

  sfile.alloc = 120;
  sfile.buffer = (char *) xmalloc (sfile.alloc);
  sfile.pos = 0;

  inf->insn_info_valid = 0;
  disassemble_set_printf (inf, &sfile, (fprintf_ftype) objdump_sprintf,
			  (fprintf_styled_ftype) objdump_styled_sprintf);

  addr_offset = start_offset;
  while (addr_offset < stop_offset)
    {
      int previous_octets;

      /* Remember the length of the previous instruction.  */
      previous_octets = octets;
      octets = 0;

      sfile.pos = 0;
      inf->bytes_per_line = 0;
      inf->bytes_per_chunk = 0;
      inf->flags = ((disassemble_all ? DISASSEMBLE_DATA : 0)
		    | (wide_output ? WIDE_OUTPUT : 0));
      if (machine)
	inf->flags |= USER_SPECIFIED_MACHINE_TYPE;

      if (inf->disassembler_needs_relocs
	  && (bfd_get_file_flags (aux->abfd) & EXEC_P) == 0
	  && (bfd_get_file_flags (aux->abfd) & DYNAMIC) == 0
	  && relpp < relppend)
	{
	  bfd_signed_vma distance_to_rel;

	  distance_to_rel = (*relpp)->address - (rel_offset + addr_offset);

	  /* Check to see if the current reloc is associated with
	     the instruction that we are about to disassemble.  */
	  if (distance_to_rel == 0
	      /* FIXME: This is wrong.  We are trying to catch
		 relocs that are addressed part way through the
		 current instruction, as might happen with a packed
		 VLIW instruction.  Unfortunately we do not know the
		 length of the current instruction since we have not
		 disassembled it yet.  Instead we take a guess based
		 upon the length of the previous instruction.  The
		 proper solution is to have a new target-specific
		 disassembler function which just returns the length
		 of an instruction at a given address without trying
		 to display its disassembly. */
	      || (distance_to_rel > 0
		&& distance_to_rel < (bfd_signed_vma) (previous_octets/ opb)))
	    {
	      inf->flags |= INSN_HAS_RELOC;
	    }
	}

      if (! disassemble_all
	  && (section->flags & (SEC_CODE | SEC_HAS_CONTENTS))
	  == (SEC_CODE | SEC_HAS_CONTENTS))
	/* Set a stop_vma so that the disassembler will not read
	   beyond the next symbol.  We assume that symbols appear on
	   the boundaries between instructions.  We only do this when
	   disassembling code of course, and when -D is in effect.  */
	inf->stop_vma = section->vma + stop_offset;

      inf->stop_offset = stop_offset;

      /* Extract jump information.  */
      inf->insn_info_valid = 0;
      disassembler_in_comment = false;
      octets = (*disassemble_fn) (section->vma + addr_offset, inf);
      /* Test if a jump was detected.  */
      if (inf->insn_info_valid
	  && ((inf->insn_type == dis_branch)
	      || (inf->insn_type == dis_condbranch)
	      || (inf->insn_type == dis_jsr)
	      || (inf->insn_type == dis_condjsr))
	  && (inf->target >= section->vma + start_offset)
	  && (inf->target < section->vma + stop_offset))
	{
	  struct jump_info *ji =
	    jump_info_new (section->vma + addr_offset, inf->target, -1);
	  jump_info_add_front (ji, &jumps);
	}

      inf->stop_vma = 0;

      addr_offset += octets / opb;
    }

  disassemble_set_printf (inf, (void *) stdout, (fprintf_ftype) fprintf,
			  (fprintf_styled_ftype) fprintf_styled);
  free (sfile.buffer);

  /* Merge jumps.  */
  jump_info_merge (&jumps);
  /* Process jumps.  */
  jump_info_sort (&jumps);

  /* Group jumps by level.  */
  struct jump_info *last_jump = jumps;
  int max_level = -1;

  while (last_jump)
    {
      /* The last jump is part of the next group.  */
      struct jump_info *base = last_jump;
      /* Increment level.  */
      base->level = ++max_level;

      /* Find jumps that can be combined on the same
	 level, with the largest jumps tested first.
	 This has the advantage that large jumps are on
	 lower levels and do not intersect with small
	 jumps that get grouped on higher levels.  */
      struct jump_info *exchange_item = last_jump->next;
      struct jump_info *it = exchange_item;

      for (; it; it = it->next)
	{
	  /* Test if the jump intersects with any
	     jump from current group.  */
	  bool ok = true;
	  struct jump_info *it_collision;

	  for (it_collision = base;
	       it_collision != exchange_item;
	       it_collision = it_collision->next)
	    {
	      /* This jump intersects so we leave it out.  */
	      if (jump_info_intersect (it_collision, it))
		{
		  ok = false;
		  break;
		}
	    }

	  /* Add jump to group.  */
	  if (ok)
	    {
	      /* Move current element to the front.  */
	      if (it != exchange_item)
		{
		  struct jump_info *save = it->prev;
		  jump_info_move_linked (it, exchange_item, &jumps);
		  last_jump = it;
		  it = save;
		}
	      else
		{
		  last_jump = exchange_item;
		  exchange_item = exchange_item->next;
		}
	      last_jump->level = max_level;
	    }
	}

      /* Move to next group.  */
      last_jump = exchange_item;
    }

  return jumps;
}

/* The number of zeroes we want to see before we start skipping them.
   The number is arbitrarily chosen.  */

#define DEFAULT_SKIP_ZEROES 8

/* The number of zeroes to skip at the end of a section.  If the
   number of zeroes at the end is between SKIP_ZEROES_AT_END and
   SKIP_ZEROES, they will be disassembled.  If there are fewer than
   SKIP_ZEROES_AT_END, they will be skipped.  This is a heuristic
   attempt to avoid disassembling zeroes inserted by section
   alignment.  */

#define DEFAULT_SKIP_ZEROES_AT_END 3

static int
null_print (const void * stream ATTRIBUTE_UNUSED, const char * format ATTRIBUTE_UNUSED, ...)
{
  return 1;
}

/* Like null_print, but takes the extra STYLE argument.  As this is not
   going to print anything, the extra argument is just ignored.  */

static int
null_styled_print (const void * stream ATTRIBUTE_UNUSED,
		   enum disassembler_style style ATTRIBUTE_UNUSED,
		   const char * format ATTRIBUTE_UNUSED, ...)
{
  return 1;
}

/* Print out jump visualization.  */

static void
print_jump_visualisation (bfd_vma addr, int max_level, char *line_buffer,
			  uint8_t *color_buffer)
{
  if (!line_buffer)
    return;

  jump_info_visualize_address (addr, max_level, line_buffer, color_buffer);

  size_t line_buffer_size = strlen (line_buffer);
  char last_color = 0;
  size_t i;

  for (i = 0; i <= line_buffer_size; ++i)
    {
      if (color_output)
	{
	  uint8_t color = (i < line_buffer_size) ? color_buffer[i]: 0;

	  if (color != last_color)
	    {
	      if (color)
		if (extended_color_output)
		  /* Use extended 8bit color, but
		     do not choose dark colors.  */
		  printf ("\033[38;5;%dm", 124 + (color % 108));
		else
		  /* Use simple terminal colors.  */
		  printf ("\033[%dm", 31 + (color % 7));
	      else
		/* Clear color.  */
		printf ("\033[0m");
	      last_color = color;
	    }
	}
      putchar ((i < line_buffer_size) ? line_buffer[i]: ' ');
    }
}

/* Disassemble some data in memory between given values.  */

static void
disassemble_bytes (struct disassemble_info *inf,
		   disassembler_ftype disassemble_fn,
		   bool insns,
		   const bfd_byte *data,
		   bfd_vma start_offset,
		   bfd_vma stop_offset,
		   bfd_vma rel_offset,
		   arelent **relpp,
		   arelent **relppend)
{
  struct objdump_disasm_info *aux;
  asection *section;
  unsigned int octets_per_line;
  unsigned int skip_addr_chars;
  bfd_vma addr_offset;
  unsigned int opb = inf->octets_per_byte;
  unsigned int skip_zeroes = inf->skip_zeroes;
  unsigned int skip_zeroes_at_end = inf->skip_zeroes_at_end;
  size_t octets;
  SFILE sfile;

  aux = (struct objdump_disasm_info *) inf->application_data;
  section = inf->section;

  sfile.alloc = 120;
  sfile.buffer = (char *) xmalloc (sfile.alloc);
  sfile.pos = 0;

  if (insn_width)
    octets_per_line = insn_width;
  else if (insns)
    octets_per_line = 4;
  else
    octets_per_line = 16;

  /* Figure out how many characters to skip at the start of an
     address, to make the disassembly look nicer.  We discard leading
     zeroes in chunks of 4, ensuring that there is always a leading
     zero remaining.  */
  skip_addr_chars = 0;
  if (!no_addresses && !prefix_addresses)
    {
      char buf[30];

      bfd_sprintf_vma (aux->abfd, buf, section->vma + section->size / opb);

      while (buf[skip_addr_chars] == '0')
	++skip_addr_chars;

      /* Don't discard zeros on overflow.  */
      if (buf[skip_addr_chars] == '\0' && section->vma != 0)
	skip_addr_chars = 0;

      if (skip_addr_chars != 0)
	skip_addr_chars = (skip_addr_chars - 1) & -4;
    }

  inf->insn_info_valid = 0;

  /* Determine maximum level. */
  uint8_t *color_buffer = NULL;
  char *line_buffer = NULL;
  int max_level = -1;

  /* Some jumps were detected.  */
  if (detected_jumps)
    {
      struct jump_info *ji;

      /* Find maximum jump level.  */
      for (ji = detected_jumps; ji; ji = ji->next)
	{
	  if (ji->level > max_level)
	    max_level = ji->level;
	}

      /* Allocate buffers.  */
      size_t len = (max_level + 1) * 3 + 1;
      line_buffer = (char*) xmalloc (len);
      line_buffer[len - 1] = 0;
      color_buffer = (uint8_t*) xmalloc (len);
      color_buffer[len - 1] = 0;
    }

  addr_offset = start_offset;
  while (addr_offset < stop_offset)
    {
      bool need_nl = false;

      octets = 0;

      /* Make sure we don't use relocs from previous instructions.  */
      aux->reloc = NULL;

      /* If we see more than SKIP_ZEROES octets of zeroes, we just
	 print `...'.  */
      if (! disassemble_zeroes)
	for (; octets < (stop_offset - addr_offset) * opb; octets++)
	  if (data[addr_offset * opb + octets] != 0)
	    break;
      if (! disassemble_zeroes
	  && (inf->insn_info_valid == 0
	      || inf->branch_delay_insns == 0)
	  && (octets >= skip_zeroes
	      || (octets == (stop_offset - addr_offset) * opb
		  && octets < skip_zeroes_at_end)))
	{
	  /* If there are more nonzero octets to follow, we only skip
	     zeroes in multiples of 4, to try to avoid running over
	     the start of an instruction which happens to start with
	     zero.  */
	  if (octets != (stop_offset - addr_offset) * opb)
	    octets &= ~3;

	  /* If we are going to display more data, and we are displaying
	     file offsets, then tell the user how many zeroes we skip
	     and the file offset from where we resume dumping.  */
	  if (display_file_offsets
	      && octets / opb < stop_offset - addr_offset)
	    oprintf (ssdisass, _("\t... (skipping %lu zeroes, "
		      "resuming at file offset: 0x%lx)\n"),
		    (unsigned long) (octets / opb),
		    (unsigned long) (section->filepos
				     + addr_offset + octets / opb));
	  else
	    oprintf (ssdisass, "\t...\n");
	}
      else
	{
	  char buf[50];
	  unsigned int bpc = 0;
	  unsigned int pb = 0;

	  if (with_line_numbers || with_source_code)
	    show_line (aux->abfd, section, addr_offset);

	  if (no_addresses)
	    oprintf (ssdisass, "\t");
	  else if (!prefix_addresses)
	    {
	      char *s;

	      bfd_sprintf_vma (aux->abfd, buf, section->vma + addr_offset);
	      for (s = buf + skip_addr_chars; *s == '0'; s++)
		*s = ' ';
	      if (*s == '\0')
		*--s = '0';
	      oprintf (ssdisass, "%s:\t", buf + skip_addr_chars);
	    }
	  else
	    {
	      aux->require_sec = true;
	      objdump_print_address (section->vma + addr_offset, inf);
	      aux->require_sec = false;
	      oprintf (ssdisass, " ");
	    }

	  print_jump_visualisation (section->vma + addr_offset,
				    max_level, line_buffer,
				    color_buffer);

	  if (insns)
	    {
	      int insn_size;

	      sfile.pos = 0;
	      disassemble_set_printf
		(inf, &sfile, (fprintf_ftype) objdump_sprintf,
		 (fprintf_styled_ftype) objdump_styled_sprintf);
	      inf->bytes_per_line = 0;
	      inf->bytes_per_chunk = 0;
	      inf->flags = ((disassemble_all ? DISASSEMBLE_DATA : 0)
			    | (wide_output ? WIDE_OUTPUT : 0));
	      if (machine)
		inf->flags |= USER_SPECIFIED_MACHINE_TYPE;

	      if (inf->disassembler_needs_relocs
		  && (bfd_get_file_flags (aux->abfd) & EXEC_P) == 0
		  && (bfd_get_file_flags (aux->abfd) & DYNAMIC) == 0
		  && relpp < relppend)
		{
		  bfd_signed_vma distance_to_rel;
		  int max_reloc_offset
		    = aux->abfd->arch_info->max_reloc_offset_into_insn;

		  distance_to_rel = ((*relpp)->address - rel_offset
				     - addr_offset);

		  insn_size = 0;
		  if (distance_to_rel > 0
		      && (max_reloc_offset < 0
			  || distance_to_rel <= max_reloc_offset))
		    {
		      /* This reloc *might* apply to the current insn,
			 starting somewhere inside it.  Discover the length
			 of the current insn so that the check below will
			 work.  */
		      if (insn_width)
			insn_size = insn_width;
		      else
			{
			  /* We find the length by calling the dissassembler
			     function with a dummy print handler.  This should
			     work unless the disassembler is not expecting to
			     be called multiple times for the same address.

			     This does mean disassembling the instruction
			     twice, but we only do this when there is a high
			     probability that there is a reloc that will
			     affect the instruction.  */
			  disassemble_set_printf
			    (inf, inf->stream, (fprintf_ftype) null_print,
			     (fprintf_styled_ftype) null_styled_print);
			  insn_size = disassemble_fn (section->vma
						      + addr_offset, inf);
			  disassemble_set_printf
			    (inf, inf->stream,
			     (fprintf_ftype) objdump_sprintf,
			     (fprintf_styled_ftype) objdump_styled_sprintf);
			}
		    }

		  /* Check to see if the current reloc is associated with
		     the instruction that we are about to disassemble.  */
		  if (distance_to_rel == 0
		      || (distance_to_rel > 0
			  && distance_to_rel < insn_size / (int) opb))
		    {
		      inf->flags |= INSN_HAS_RELOC;
		      aux->reloc = *relpp;
		    }
		}

	      if (! disassemble_all
		  && ((section->flags & (SEC_CODE | SEC_HAS_CONTENTS))
		      == (SEC_CODE | SEC_HAS_CONTENTS)))
		/* Set a stop_vma so that the disassembler will not read
		   beyond the next symbol.  We assume that symbols appear on
		   the boundaries between instructions.  We only do this when
		   disassembling code of course, and when -D is in effect.  */
		inf->stop_vma = section->vma + stop_offset;

	      inf->stop_offset = stop_offset;
	      disassembler_in_comment = false;
	      insn_size = (*disassemble_fn) (section->vma + addr_offset, inf);
	      octets = insn_size;

	      inf->stop_vma = 0;
	      disassemble_set_printf (inf, stdout, (fprintf_ftype) fprintf,
				      (fprintf_styled_ftype) fprintf_styled);
	      if (insn_width == 0 && inf->bytes_per_line != 0)
		octets_per_line = inf->bytes_per_line;
	      if (insn_size < (int) opb)
		{
		  if (sfile.pos)
		    oprintf (ssdisass, "%s\n", sfile.buffer);
		  if (insn_size >= 0)
		    {
		      non_fatal (_("disassemble_fn returned length %d"),
				 insn_size);
		      exit_status = 1;
		    }
		  break;
		}
	    }
	  else
	    {
	      bfd_vma j;

	      octets = octets_per_line;
	      if (octets / opb > stop_offset - addr_offset)
		octets = (stop_offset - addr_offset) * opb;

	      for (j = addr_offset * opb; j < addr_offset * opb + octets; ++j)
		{
		  if (ISPRINT (data[j]))
		    buf[j - addr_offset * opb] = data[j];
		  else
		    buf[j - addr_offset * opb] = '.';
		}
	      buf[j - addr_offset * opb] = '\0';
	    }

	  if (prefix_addresses
	      ? show_raw_insn > 0
	      : show_raw_insn >= 0)
	    {
	      bfd_vma j;

	      /* If ! prefix_addresses and ! wide_output, we print
		 octets_per_line octets per line.  */
	      pb = octets;
	      if (pb > octets_per_line && ! prefix_addresses && ! wide_output)
		pb = octets_per_line;

	      if (inf->bytes_per_chunk)
		bpc = inf->bytes_per_chunk;
	      else
		bpc = 1;

	      for (j = addr_offset * opb; j < addr_offset * opb + pb; j += bpc)
		{
		  /* PR 21580: Check for a buffer ending early.  */
		  if (j + bpc <= stop_offset * opb)
		    {
		      unsigned int k;

		      if (inf->display_endian == BFD_ENDIAN_LITTLE)
			{
			  for (k = bpc; k-- != 0; )
			    oprintf (ssdisass, "%02x", (unsigned) data[j + k]);
			}
		      else
			{
			  for (k = 0; k < bpc; k++)
			    oprintf (ssdisass, "%02x", (unsigned) data[j + k]);
			}
		    }
		  oprintf (ssdisass, " ");
		}

	      for (; pb < octets_per_line; pb += bpc)
		{
		  unsigned int k;

		  for (k = 0; k < bpc; k++)
		    oprintf (ssdisass, "  ");
		  oprintf (ssdisass, " ");
		}

	      /* Separate raw data from instruction by extra space.  */
	      if (insns)
		oprintf (ssdisass, "\t");
	      else
		oprintf (ssdisass, "    ");
	    }

	  if (! insns)
	    oprintf (ssdisass, "%s", buf);
	  else if (sfile.pos)
	    oprintf (ssdisass, "%s", sfile.buffer);

	  if (prefix_addresses
	      ? show_raw_insn > 0
	      : show_raw_insn >= 0)
	    {
	      while (pb < octets)
		{
		  bfd_vma j;
		  char *s;

		  oprintf (ssdisass, "\n");
		  j = addr_offset * opb + pb;

		  if (no_addresses)
		    oprintf (ssdisass, "\t");
		  else
		    {
		      bfd_sprintf_vma (aux->abfd, buf, section->vma + j / opb);
		      for (s = buf + skip_addr_chars; *s == '0'; s++)
			*s = ' ';
		      if (*s == '\0')
			*--s = '0';
		      oprintf (ssdisass, "%s:\t", buf + skip_addr_chars);
		    }

		  print_jump_visualisation (section->vma + j / opb,
					    max_level, line_buffer,
					    color_buffer);

		  pb += octets_per_line;
		  if (pb > octets)
		    pb = octets;
		  for (; j < addr_offset * opb + pb; j += bpc)
		    {
		      /* PR 21619: Check for a buffer ending early.  */
		      if (j + bpc <= stop_offset * opb)
			{
			  unsigned int k;

			  if (inf->display_endian == BFD_ENDIAN_LITTLE)
			    {
			      for (k = bpc; k-- != 0; )
				oprintf (ssdisass, "%02x", (unsigned) data[j + k]);
			    }
			  else
			    {
			      for (k = 0; k < bpc; k++)
				oprintf (ssdisass, "%02x", (unsigned) data[j + k]);
			    }
			}
		      oprintf (ssdisass, " ");
		    }
		}
	    }

	  if (!wide_output)
	    oprintf (ssdisass, "\n");
	  else
	    need_nl = true;
	}

      while (relpp < relppend
	     && (*relpp)->address < rel_offset + addr_offset + octets / opb)
	{
	  if (dump_reloc_info || dump_dynamic_reloc_info)
	    {
	      arelent *q;

	      q = *relpp;

	      if (wide_output)
		oprintf (ssdisass, "\t");
	      else
		oprintf (ssdisass, "\t\t\t");

	      if (!no_addresses)
		{
		  objdump_print_value (section->vma - rel_offset + q->address,
				       inf, true);
		  oprintf (ssdisass, ": ");
		}

	      if (q->howto == NULL)
		oprintf (ssdisass, "*unknown*\t");
	      else if (q->howto->name)
		oprintf (ssdisass, "%s\t", q->howto->name);
	      else
		oprintf (ssdisass, "%d\t", q->howto->type);

	      if (q->sym_ptr_ptr == NULL || *q->sym_ptr_ptr == NULL)
		oprintf (ssdisass, "*unknown*");
	      else
		{
		  const char *sym_name;

		  sym_name = bfd_asymbol_name (*q->sym_ptr_ptr);
		  if (sym_name != NULL && *sym_name != '\0')
		    objdump_print_symname (aux->abfd, inf, *q->sym_ptr_ptr);
		  else
		    {
		      asection *sym_sec;

		      sym_sec = bfd_asymbol_section (*q->sym_ptr_ptr);
		      sym_name = bfd_section_name (sym_sec);
		      if (sym_name == NULL || *sym_name == '\0')
			sym_name = "*unknown*";
		      oprintf (ssdisass, "%s", sanitize_string (sym_name));
		    }
		}

	      if (q->addend)
		{
		  bfd_vma addend = q->addend;
		  if ((bfd_signed_vma) addend < 0)
		    {
		      oprintf (ssdisass, "-0x");
		      addend = -addend;
		    }
		  else
		    oprintf (ssdisass, "+0x");
		  objdump_print_value (addend, inf, true);
		}

	      oprintf (ssdisass, "\n");
	      need_nl = false;
	    }
	  ++relpp;
	}

      if (need_nl)
	oprintf (ssdisass, "\n");

      addr_offset += octets / opb;
    }

  free (sfile.buffer);
  free (line_buffer);
  free (color_buffer);
}

static void
disassemble_section (
  const bfd_byte *data, bfd_size_type datasize,
  bfd *abfd, asection *section, struct disassemble_info *pinfo)
{
  const struct elf_backend_data *bed;
  bfd_vma sign_adjust = 0;
  struct objdump_disasm_info *paux;
  unsigned int opb = pinfo->octets_per_byte;
  arelent **rel_pp = NULL;
  arelent **rel_ppstart = NULL;
  arelent **rel_ppend;
  bfd_vma stop_offset;
  asymbol *sym = NULL;
  long place = 0;
  long rel_count;
  bfd_vma rel_offset;
  unsigned long addr_offset;
  bool do_print;
  enum loop_control
  {
   stop_offset_reached,
   function_sym,
   next_sym
  } loop_until;

  if (only_list == NULL)
    {
      /* Sections that do not contain machine
	 code are not normally disassembled.  */
      if ((section->flags & SEC_HAS_CONTENTS) == 0)
	return;

      if (! disassemble_all
	  && (section->flags & SEC_CODE) == 0)
	return;
    }
  else if (!process_section_p (section))
    return;

  if (datasize == 0)
    return;

  if (start_address == (bfd_vma) -1
      || start_address < section->vma)
    addr_offset = 0;
  else
    addr_offset = start_address - section->vma;

  if (stop_address == (bfd_vma) -1)
    stop_offset = datasize / opb;
  else
    {
      if (stop_address < section->vma)
	stop_offset = 0;
      else
	stop_offset = stop_address - section->vma;
      if (stop_offset > datasize / opb)
	stop_offset = datasize / opb;
    }

  if (addr_offset >= stop_offset)
    return;

  /* Decide which set of relocs to use.  Load them if necessary.  */
  paux = (struct objdump_disasm_info *) pinfo->application_data;
  if (pinfo->dynrelbuf && dump_dynamic_reloc_info)
    {
      rel_pp = pinfo->dynrelbuf;
      rel_count = pinfo->dynrelcount;
      /* Dynamic reloc addresses are absolute, non-dynamic are section
	 relative.  REL_OFFSET specifies the reloc address corresponding
	 to the start of this section.  */
      rel_offset = section->vma;
    }
  else
    {
      rel_count = 0;
      rel_pp = NULL;
      rel_offset = 0;

      if ((section->flags & SEC_RELOC) != 0
	  && (dump_reloc_info || pinfo->disassembler_needs_relocs))
	{
	  long relsize;

	  relsize = bfd_get_reloc_upper_bound (abfd, section);
	  if (relsize < 0)
	    my_bfd_nonfatal (bfd_get_filename (abfd));

	  if (relsize > 0)
	    {
	      rel_pp = (arelent **) xmalloc (relsize);
	      rel_count = bfd_canonicalize_reloc (abfd, section, rel_pp, syms);
	      if (rel_count < 0)
		{
		  my_bfd_nonfatal (bfd_get_filename (abfd));
		  free (rel_pp);
		  rel_pp = NULL;
		  rel_count = 0;
		}
	      else if (rel_count > 1)
		/* Sort the relocs by address.  */
		qsort (rel_pp, rel_count, sizeof (arelent *), compare_relocs);
	      rel_ppstart = rel_pp;
	    }
	}
    }
  rel_ppend = PTR_ADD (rel_pp, rel_count);

  pinfo->buffer = (bfd_byte*) data;
  pinfo->buffer_vma = section->vma;
  pinfo->buffer_length = datasize;
  pinfo->section = section;

  /* Sort the symbols into value and section order.  */
  compare_section = section;
  if (sorted_symcount > 1)
    qsort (sorted_syms, sorted_symcount, sizeof (asymbol *), compare_symbols);

  /* Find the nearest symbol forwards from our current position.  */
  paux->require_sec = true;
  sym = (asymbol *) find_symbol_for_address (section->vma + addr_offset,
					     pinfo, &place);
  paux->require_sec = false;

  /* PR 9774: If the target used signed addresses then we must make
     sure that we sign extend the value that we calculate for 'addr'
     in the loop below.  */
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour
      && (bed = get_elf_backend_data (abfd)) != NULL
      && bed->sign_extend_vma)
    sign_adjust = (bfd_vma) 1 << (bed->s->arch_size - 1);

  /* Disassemble a block of instructions up to the address associated with
     the symbol we have just found.  Then print the symbol and find the
     next symbol on.  Repeat until we have disassembled the entire section
     or we have reached the end of the address range we are interested in.  */
  do_print = paux->symbol == NULL;
  loop_until = stop_offset_reached;

  while (addr_offset < stop_offset)
    {
      bfd_vma addr;
      asymbol *nextsym;
      bfd_vma nextstop_offset;
      bool insns;

      /* Skip over the relocs belonging to addresses below the
	 start address.  */
      while (rel_pp < rel_ppend
	     && (*rel_pp)->address < rel_offset + addr_offset)
	++rel_pp;

      addr = section->vma + addr_offset;
      addr = ((addr & ((sign_adjust << 1) - 1)) ^ sign_adjust) - sign_adjust;

      if (sym != NULL && bfd_asymbol_value (sym) <= addr)
	{
	  int x;

	  for (x = place;
	       (x < sorted_symcount
		&& (bfd_asymbol_value (sorted_syms[x]) <= addr));
	       ++x)
	    continue;

	  pinfo->symbols = sorted_syms + place;
	  pinfo->num_symbols = x - place;
	  pinfo->symtab_pos = place;
	}
      else
	{
	  pinfo->symbols = NULL;
	  pinfo->num_symbols = 0;
	  pinfo->symtab_pos = -1;
	}

      /* If we are only disassembling from a specific symbol,
	 check to see if we should start or stop displaying.  */
      if (sym && paux->symbol)
	{
	  if (do_print)
	    {
	      /* See if we should stop printing.  */
	      switch (loop_until)
		{
		case function_sym:
		  if (sym->flags & BSF_FUNCTION)
		    do_print = false;
		  break;

		case stop_offset_reached:
		  /* Handled by the while loop.  */
		  break;

		case next_sym:
		  /* FIXME: There is an implicit assumption here
		     that the name of sym is different from
		     paux->symbol.  */
		  if (! bfd_is_local_label (abfd, sym))
		    do_print = false;
		  break;
		}
	    }
	  else
	    {
	      const char * name = bfd_asymbol_name (sym);
	      char * alloc = NULL;

	      if (do_demangle && name[0] != '\0')
		{
		  /* Demangle the name.  */
		  alloc = bfd_demangle (abfd, name, demangle_flags);
		  if (alloc != NULL)
		    name = alloc;
		}

	      /* We are not currently printing.  Check to see
		 if the current symbol matches the requested symbol.  */
	      if (streq (name, paux->symbol)
		  && bfd_asymbol_value (sym) <= addr)
		{
		  do_print = true;

		  loop_until = next_sym;
		  if (sym->flags & BSF_FUNCTION)
		    {
		      loop_until = function_sym;

		      if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
			{
			  bfd_size_type fsize =
			    ((elf_symbol_type *) sym)->internal_elf_sym.st_size;
			  bfd_vma fend =
			    bfd_asymbol_value (sym) - section->vma + fsize;
			  if (fend > addr_offset && fend <= stop_offset)
			    {
			      /* Sym is a function symbol with a valid
				 size associated with it.  Disassemble
				 to the end of the function.  */
			      stop_offset = fend;
			      loop_until = stop_offset_reached;
			    }
			}
		    }
		}

	      free (alloc);
	    }
	}

      if (sym != NULL && bfd_asymbol_value (sym) > addr)
	nextsym = sym;
      else if (sym == NULL)
	nextsym = NULL;
      else
	{
#define is_valid_next_sym(SYM) \
  (strcmp (bfd_section_name ((SYM)->section), bfd_section_name (section)) == 0 \
   && (bfd_asymbol_value (SYM) > bfd_asymbol_value (sym)) \
   && pinfo->symbol_is_valid (SYM, pinfo))

	  /* Search forward for the next appropriate symbol in
	     SECTION.  Note that all the symbols are sorted
	     together into one big array, and that some sections
	     may have overlapping addresses.  */
	  while (place < sorted_symcount
		 && ! is_valid_next_sym (sorted_syms [place]))
	    ++place;

	  if (place >= sorted_symcount)
	    nextsym = NULL;
	  else
	    nextsym = sorted_syms[place];
	}

      if (sym != NULL && bfd_asymbol_value (sym) > addr)
	nextstop_offset = bfd_asymbol_value (sym) - section->vma;
      else if (nextsym == NULL)
	nextstop_offset = stop_offset;
      else
	nextstop_offset = bfd_asymbol_value (nextsym) - section->vma;

      if (nextstop_offset > stop_offset
	  || nextstop_offset <= addr_offset)
	nextstop_offset = stop_offset;

      /* If a symbol is explicitly marked as being an object
	 rather than a function, just dump the bytes without
	 disassembling them.  */
      if (disassemble_all
	  || sym == NULL
	  || sym->section != section
	  || bfd_asymbol_value (sym) > addr
	  || ((sym->flags & BSF_OBJECT) == 0
	      && (strstr (bfd_asymbol_name (sym), "gnu_compiled")
		  == NULL)
	      && (strstr (bfd_asymbol_name (sym), "gcc2_compiled")
		  == NULL))
	  || (sym->flags & BSF_FUNCTION) != 0)
	insns = true;
      else
	insns = false;

      if (do_print)
	{
	  /* Resolve symbol name.  */
	  if (visualize_jumps && abfd && sym && sym->name)
	    {
	      struct disassemble_info di;
	      SFILE sf;

	      sf.alloc = strlen (sym->name) + 40;
	      sf.buffer = (char*) xmalloc (sf.alloc);
	      sf.pos = 0;
	      disassemble_set_printf
		(&di, &sf, (fprintf_ftype) objdump_sprintf,
		 (fprintf_styled_ftype) objdump_styled_sprintf);

	      objdump_print_symname (abfd, &di, sym);

	      /* Fetch jump information.  */
	      detected_jumps = disassemble_jumps (pinfo, paux->disassemble_fn,
						  addr_offset, nextstop_offset,
						  rel_offset, rel_pp, rel_ppend);
	      /* Free symbol name.  */
	      free (sf.buffer);
	    }

	  /* Add jumps to output.  */
	  disassemble_bytes (pinfo, paux->disassemble_fn, insns, data,
			     addr_offset, nextstop_offset,
			     rel_offset, rel_pp, rel_ppend);

	  /* Free jumps.  */
	  while (detected_jumps)
	    {
	      detected_jumps = jump_info_free (detected_jumps);
	    }
	}

      addr_offset = nextstop_offset;
      sym = nextsym;
    }

  free (rel_ppstart);
}

/* Disassemble the contents of an object file.  */

static void
disassemble_data (const bfd_byte *data, bfd_size_type datasize, bfd *abfd)
{
  struct disassemble_info disasm_info;
  struct objdump_disasm_info aux;

  print_files = NULL;
  prev_functionname = NULL;
  prev_line = -1;
  prev_discriminator = 0;

  init_disassemble_info (&disasm_info, stdout, (fprintf_ftype) fprintf,
			 (fprintf_styled_ftype) fprintf_styled);
  disasm_info.application_data = (void *) &aux;
  aux.abfd = abfd;
  aux.require_sec = false;
  disasm_info.dynrelbuf = NULL;
  disasm_info.dynrelcount = 0;
  aux.reloc = NULL;
  aux.symbol = disasm_sym;

  disasm_info.print_address_func = objdump_print_address;
  disasm_info.symbol_at_address_func = objdump_symbol_at_address;

  if (machine != NULL)
    {
      const bfd_arch_info_type *inf = bfd_scan_arch (machine);

      if (inf == NULL)
	{
	  non_fatal (_("can't use supplied machine %s"), machine);
	  exit_status = 1;
	}
      else
	abfd->arch_info = inf;
    }

  if (endian != BFD_ENDIAN_UNKNOWN)
    {
      struct bfd_target *xvec;

      xvec = (struct bfd_target *) xmalloc (sizeof (struct bfd_target));
      memcpy (xvec, abfd->xvec, sizeof (struct bfd_target));
      xvec->byteorder = endian;
      abfd->xvec = xvec;
    }

  /* Use libopcodes to locate a suitable disassembler.  */
  aux.disassemble_fn = disassembler (bfd_get_arch (abfd),
				     bfd_big_endian (abfd),
				     bfd_get_mach (abfd), abfd);
  if (!aux.disassemble_fn)
    {
      non_fatal (_("can't disassemble for architecture %s\n"),
		 bfd_printable_arch_mach (bfd_get_arch (abfd), 0));
      exit_status = 1;
      return;
    }

  disasm_info.flavour = bfd_get_flavour (abfd);
  disasm_info.arch = bfd_get_arch (abfd);
  disasm_info.mach = bfd_get_mach (abfd);
  disasm_info.disassembler_options = disassembler_options;
  disasm_info.octets_per_byte = bfd_octets_per_byte (abfd, NULL);
  disasm_info.skip_zeroes = DEFAULT_SKIP_ZEROES;
  disasm_info.skip_zeroes_at_end = DEFAULT_SKIP_ZEROES_AT_END;
  disasm_info.disassembler_needs_relocs = false;

  if (bfd_big_endian (abfd))
    disasm_info.display_endian = disasm_info.endian = BFD_ENDIAN_BIG;
  else if (bfd_little_endian (abfd))
    disasm_info.display_endian = disasm_info.endian = BFD_ENDIAN_LITTLE;
  else
    /* ??? Aborting here seems too drastic.  We could default to big or little
       instead.  */
    disasm_info.endian = BFD_ENDIAN_UNKNOWN;

  disasm_info.endian_code = disasm_info.endian;

  /* Allow the target to customize the info structure.  */
  disassemble_init_for_target (& disasm_info);

  /* Pre-load the dynamic relocs as we may need them during the disassembly.  */
  long relsize = bfd_get_dynamic_reloc_upper_bound (abfd);

  if (relsize > 0)
    {
      disasm_info.dynrelbuf = (arelent **) xmalloc (relsize);
      disasm_info.dynrelcount
	= bfd_canonicalize_dynamic_reloc (abfd, disasm_info.dynrelbuf, dynsyms);
      if (disasm_info.dynrelcount < 0)
	{
	  my_bfd_nonfatal (bfd_get_filename (abfd));
	  free (disasm_info.dynrelbuf);
	  disasm_info.dynrelbuf = NULL;
	  disasm_info.dynrelcount = 0;
	}
      else if (disasm_info.dynrelcount > 1)
	/* Sort the relocs by address.  */
	qsort (disasm_info.dynrelbuf, disasm_info.dynrelcount,
	       sizeof (arelent *), compare_relocs);
    }

  disasm_info.symtab = nullptr;
  disasm_info.symtab_size = sorted_symcount;

  bfd_symbol symbol;
  memset(&symbol, 0, sizeof(symbol));
  asection section;
  memset(&section, 0, sizeof(section));
  
  symbol.the_bfd = abfd;
  symbol.name = ".data";
  symbol.flags = 256;
  symbol.section = &section;
  
  section.name = ".data";
  section.id = 16;
  section.flags = 291;
  section.size = datasize;
  section.owner = abfd;
  section.symbol = &symbol;

  disassemble_section (
    data, datasize,
    abfd, &section, &disasm_info);

  free (disasm_info.dynrelbuf);
  disasm_info.dynrelbuf = NULL;
  disassemble_free_target (&disasm_info);
}

extern const bfd_target binary_vec;

/* Dump selected contents of ABFD.  */

static void
display_file (const char* data, size_t datasize)
{
  bfd abfd;
  memset(&abfd, 0, sizeof(abfd));
  abfd.xvec = &binary_vec;

  disassemble_data ((const bfd_byte *) data, (bfd_size_type) datasize, &abfd);
}

namespace {

class BinutilsDisassembler
{
public:

  BinutilsDisassembler()
  {
    setlocale (LC_CTYPE, "");

    bindtextdomain (PACKAGE, LOCALEDIR);
    textdomain (PACKAGE);

    program_name = "disass";
    xmalloc_set_program_name (program_name);
    bfd_set_error_program_name (program_name);

    if (bfd_init () != BFD_INIT_MAGIC)
      fatal (_("fatal error: libbfd ABI mismatch"));
    set_default_bfd_target ();

    disassemble = true;
    disassemble_all = true;
  }

  std::string run(const std::string& binary, const std::string& mcpu, [[maybe_unused]] uint64_t offset)
  {
    machine = mcpu.c_str();
    
    // Reset stream by swapping it with a default constructed stringstream
    std::stringstream().swap(ssdisass);
        
    display_file (binary.data(), binary.size());
    return ssdisass.str();
  }
};

} // namespace

std::string disass::disass(const std::string& binary, const std::string& mcpu, uint64_t offset)
{
    static BinutilsDisassembler d;

	// TODO Use offset (start address)
	return d.run(binary, mcpu, offset);
}

inline std::string trim(std::string& str)
{
    str.erase(str.find_last_not_of(' ')+1);         //suffixing spaces
    str.erase(0, str.find_first_not_of(' '));       //prefixing spaces
    return str;
}

nlohmann::json disass::disass_json(const std::string& binary, const std::string& mcpu, uint64_t offset, bool detail) {
    std::unordered_map<std::string, nlohmann::json> instructionsMap;
    AssemblyParser parser(binary, offset, detail, mcpu, "");

    Instruction instruction;
    while (parser.next_instruction(instruction)) {
        nlohmann::json instructionJson = {
            {"binary", instruction.binary},
            {"mnemonic", instruction.mnemonic},
            {"op_str", instruction.op_str},
            {"size", instruction.size}
        };

        if (instruction.constant) {
            instructionJson["constant"] = *instruction.constant;
        }

        instructionsMap[std::to_string(instruction.address)] = instructionJson;
    }

    return nlohmann::json(instructionsMap);
}

AssemblyParser::AssemblyParser(
    const std::string& binary, uint64_t offset_, bool& detail_,
    const std::string& cpu_model, [[maybe_unused]] const std::string& triple) :
    offset(offset_), detail(detail_)
{
    // At this time, we disassemble the entire binary at once,
    // and get a single string for the whole binary. Later we
    // may change this into line-by-line retrieval, but currently
    // there is no need for that. However, we design the APIs to
    // be capable of this already now.
    stream = std::istringstream(disass(binary, cpu_model, offset));
}
    
bool AssemblyParser::next_instruction(Instruction& instruction)
{
    bool exists = false;
    std::string line;
    while (!exists && std::getline(stream, line)) {
        if (line.empty() || line[0] != ' ') continue;

        exists = true;
        std::istringstream lineStream(line);
        std::string addressStr, binstr, mnemonic, op_str;
        lineStream >> addressStr >> binstr >> mnemonic;
        std::getline(lineStream, op_str);
        op_str = op_str.empty() ? "" : op_str.substr(1); // Remove leading tab

        std::string constant;
        size_t atPos = op_str.find_last_of('@');
        if (atPos != std::string::npos) {
            constant = op_str.substr(atPos + 1);
            op_str = op_str.substr(0, atPos - 1); // Remove "@ constant"
        }

        int address = std::stoi(addressStr, nullptr, 16) + offset;
        int size = binstr.length() / 2;

        instruction.address = address;
        instruction.binary = binstr;
        instruction.mnemonic = mnemonic;
        instruction.op_str = op_str;
        instruction.size = size;

        if (detail) {
            std::vector<Operand> operands;
            std::istringstream opStream(op_str);
            std::string op;

            while (std::getline(opStream, op, ',')) {
                op = trim(op);
                Operand operand;
                operand.text = op;
                
                // Check if the operand is an immediate value
                int base = 0;
                if (op.length() > 2 && op[0] == '0' && op[1] == 'x')
                    base = 16;
                else if (op.length() > 1 && op[0] == '#') {
                    op[0] = ' ';
                    base = 10;
                }
                
                if (base != 0) {
                    uint64_t intValue = std::stoull(op, nullptr, base);
                    uint32_t immValue = static_cast<uint32_t>(intValue);
                    Value value;
                    value.imm = immValue;
                    operand.value = value;
                }

                operands.push_back(operand);
            }

            instruction.operands = operands;
        }

        if (!constant.empty()) {
            instruction.constant = constant;
        }
    }
    
    return exists;
}

InstructionIterator::InstructionIterator(AssemblyParser* parser_) :
    parser(parser_), hasNext(parser_ ? parser_->next_instruction(currentInstruction) : false) {}

