/* Routines for dealing with '\0' separated environment vectors

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <malloc.h>
#include <string.h>

#include "envz.h"

/* The character separating names from values in an envz.  */
#define SEP '='

/* Returns a pointer to the entry in ENVZ for NAME, or 0 if there is none.
   If NAME contains the separator character, only the portion before it is
   used in the comparison.  */
char *
envz_entry (char *envz, unsigned envz_len, char *name)
{
  while (envz_len)
    {
      char *p = name;
      char *entry = envz;	/* Start of this entry. */

      /* See how far NAME and ENTRY match.  */
      while (envz_len && *p == *envz && *p && *p != SEP)
	p++, envz++, envz_len--;

      if ((*envz == '\0' || *envz == SEP) && (*p == '\0' || *p == SEP))
	/* Bingo! */
	return entry;

      /* No match, skip to the next entry.  */
      while (envz_len && *envz)
	envz++, envz_len--;
      if (envz_len)
	envz++, envz_len--;	/* skip '\0' */
    }

  return 0;
}

/* Returns a pointer to the value portion of the entry in ENVZ for NAME, or 0
   if there is none.  */
char *
envz_get (char *envz, unsigned envz_len, char *name)
{
  char *entry = envz_entry (envz, envz_len, name);
  if (entry)
    {
      while (*entry && *entry != SEP)
	entry++;
      if (*entry)
	entry++;
      else
	entry = 0;		/* A null entry.  */
    }
  return entry;
}

/* Remove the entry for NAME from ENVZ & ENVZ_LEN, if any.  */
void
envz_remove (char **envz, unsigned *envz_len, char *name)
{
  char *entry = envz_entry (*envz, *envz_len, name);
  if (entry)
    argz_delete (envz, envz_len, entry);
}

/* Adds an entry for NAME with value VALUE to ENVZ & ENVZ_LEN.  If an entry
   with the same name already exists in ENVZ, it is removed.  If VALUE is
   NULL, then the new entry will a special null one, for which envz_get will
   return NULL, although envz_entry will still return an entry; this is handy
   because when merging with another envz, the null entry can override an
   entry in the other one.  Null entries can be removed with envz_strip ().  */
error_t
envz_add (char **envz, unsigned *envz_len, char *name, char *value)
{
  envz_remove (envz, envz_len, name);

  if (value)
    /* Add the new value, if there is one.  */
    {
      unsigned name_len = strlen (name);
      unsigned value_len = strlen (value);
      unsigned old_envz_len = *envz_len;
      unsigned new_envz_len = old_envz_len + name_len + 1 + value_len + 1;
      char *new_envz = realloc (*envz, new_envz_len);

      if (new_envz)
	{
	  bcopy (name, new_envz + old_envz_len, name_len);
	  new_envz[old_envz_len + name_len] = SEP;
	  bcopy (value, new_envz + old_envz_len + name_len + 1, value_len);
	  new_envz[new_envz_len - 1] = 0;

	  *envz = new_envz;
	  *envz_len = new_envz_len;

	  return 0;
	}
      else
	return ENOMEM;
    }
  else
    /* Add a null entry.  */
    return argz_add (envz, envz_len, name);
}

/* Adds each entry in ENVZ2 to ENVZ & ENVZ_LEN, as if with envz_add().  If
   OVERRIDE is true, then values in ENVZ2 will supercede those with the same
   name in ENV, otherwise not.  */
error_t
envz_merge (char **envz, unsigned *envz_len, char *envz2, unsigned envz2_len,
	    int override)
{
  error_t err = 0;

  while (envz2_len && ! err)
    {
      char *old = envz_entry (*envz, *envz_len, envz2);
      char new_len = strlen (envz2) + 1;

      if (! old)
	err = argz_append (envz, envz_len, envz2, new_len);
      else if (override)
	{
	  argz_delete (envz, envz_len, old);
	  err = argz_append (envz, envz_len, envz2, new_len);
	}

      envz2 += new_len;
      envz2_len -= new_len;
    }

  return err;
}

/* Remove null entries.  */
void
envz_strip (char **envz, unsigned *envz_len)
{
  char *entry = *envz;
  unsigned left = *envz_len;
  while (left)
    {
      unsigned entry_len = strlen (entry) + 1;
      left -= entry_len;
      if (! index (entry, SEP))
	/* Null entry. */
	bcopy (entry, entry + entry_len, left);
      else
	entry += entry_len;
    }
  *envz_len = entry - *envz;
}
