/* File block to disk block mapping routines

   Copyright (C) 1995 Free Software Foundation, Inc.

   Converted to work under the hurd by Miles Bader <miles@gnu.ai.mit.edu>

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

/*
 *  linux/fs/ext2/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

#include <string.h>
#include "ext2fs.h"

/* 
 * ext2_discard_prealloc and ext2_alloc_block are atomic wrt. the
 * superblock in the same manner as are ext2_free_blocks and
 * ext2_new_block.  We just wait on the super rather than locking it
 * here, since ext2_new_block will do the necessary locking and we
 * can't block until then.
 */
void 
ext2_discard_prealloc (struct node *node)
{
#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count)
    {
      int i = node->dn->info.i_prealloc_count;
      ext2_debug ("discarding %d prealloced blocks for inode %d",
		  i, node->dn->number);
      node->dn->info.i_prealloc_count = 0;
      ext2_free_blocks (node->dn->info.i_prealloc_block, i);
    }
#endif
}

/* Allocate a new block for the file NODE, as close to block GOAL as
   possible, and return it, or 0 if none could be had.  If ZERO is true, then
   zero the block (and add it to NODE's list of modified indirect blocks).  */
static block_t
ext2_alloc_block (struct node *node, block_t goal, int zero)
{
#ifdef EXT2FS_DEBUG
  static unsigned long alloc_hits = 0, alloc_attempts = 0;
#endif
  block_t result;

#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count &&
      (goal == node->dn->info.i_prealloc_block ||
       goal + 1 == node->dn->info.i_prealloc_block))
    {
      result = node->dn->info.i_prealloc_block++;
      node->dn->info.i_prealloc_count--;
      ext2_debug ("preallocation hit (%lu/%lu) => %lu",
		  ++alloc_hits, ++alloc_attempts, result);
    }
  else
    {
      ext2_debug ("preallocation miss (%lu/%lu)",
		  alloc_hits, ++alloc_attempts);
      ext2_discard_prealloc (node);
      if (S_ISREG (node->dn_stat.st_mode))
	result =
	  ext2_new_block (goal,
			  &node->dn->info.i_prealloc_count,
			  &node->dn->info.i_prealloc_block);
      else
	result = ext2_new_block (goal, 0, 0);
    }
#else
  result = ext2_new_block (goal, 0, 0);
#endif

  if (result && zero)
    {
      char *bh = bptr (result);
      bzero (bh, block_size);
      record_indir_poke (node, bh);
    }

  return result;
}

static error_t
inode_getblk (struct node *node, int nr, int create, int zero,
	      block_t new_block, block_t *result)
{
  int i;
  block_t goal = 0;
#ifdef EXT2FS_DEBUG
  block_t hint;
#endif

  *result = node->dn->info.i_data[nr];
  if (*result)
    return 0;

  if (!create)
    return EINVAL;

  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;

#ifdef EXT2FS_DEBUG
  hint = goal;
#endif

  if (!goal)
    {
      for (i = nr - 1; i >= 0; i--)
	{
	  if (node->dn->info.i_data[i])
	    {
	      goal = node->dn->info.i_data[i];
	      break;
	    }
	}
      if (!goal)
	goal =
	  (node->dn->info.i_block_group * EXT2_BLOCKS_PER_GROUP (sblock))
	  + sblock->s_first_data_block;
    }

  *result = ext2_alloc_block (node, goal, zero);

  ext2_debug ("%screate, hint = %lu, goal = %lu => %lu",
	      create ? "" : "no", hint, goal, *result);

  if (!*result)
    return ENOSPC;

  node->dn->info.i_data[nr] = *result;

  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = *result;
  node->dn_set_ctime = 1;
  node->dn_stat.st_blocks += 1 << log2_stat_blocks_per_fs_block;
  node->dn_stat_dirty = 1;

  if (diskfs_synchronous || node->dn->info.i_osync)
    diskfs_node_update (node, 1);

  return 0;
}

error_t
block_getblk (struct node *node, block_t block, int nr, int create, int zero,
	      block_t new_block, block_t *result)
{
  int i;
  block_t goal = 0;
  block_t *bh = (block_t *)bptr (block);

  *result = bh[nr];
  if (*result)
    return 0;

  if (!create)
    return EINVAL;

  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;
  if (!goal)
    {
      for (i = nr - 1; i >= 0; i--)
	{
	  if (bh[i])
	    {
	      goal = bh[i];
	      break;
	    }
	}
      if (!goal)
	goal = block;
    }

  *result = ext2_alloc_block (node, goal, zero);
  if (!*result)
    return ENOSPC;

  bh[nr] = *result;

  if (diskfs_synchronous || node->dn->info.i_osync)
    sync_global_ptr (bh, 1);
  else
    record_indir_poke (node, bh);

  node->dn_set_ctime = 1;
  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = *result;
  node->dn_stat.st_blocks += 1 << log2_stat_blocks_per_fs_block;
  node->dn_stat_dirty = 1;

  return 0;
}

/* Returns in DISK_BLOCK the disk block correspding to BLOCK in NODE.  If
   there is no such block yet, but CREATE is true, then it is created,
   otherwise EINVAL is returned.  */
error_t
ext2_getblk (struct node *node, block_t block, int create, block_t *disk_block)
{
  error_t err;
  block_t indir, b;
  unsigned long addr_per_block = EXT2_ADDR_PER_BLOCK (sblock);

  if (block > EXT2_NDIR_BLOCKS + addr_per_block +
      addr_per_block * addr_per_block +
      addr_per_block * addr_per_block * addr_per_block)
    {
      ext2_warning ("block > big: %lu", block);
      return EIO;
    }
  /*
     * If this is a sequential block allocation, set the next_alloc_block
     * to this block now so that all the indblock and data block
     * allocations use the same goal zone
   */

  ext2_debug ("block = %lu, next = %lu, goal = %lu", block,
	      node->dn->info.i_next_alloc_block,
	      node->dn->info.i_next_alloc_goal);

  if (block == node->dn->info.i_next_alloc_block + 1)
    {
      node->dn->info.i_next_alloc_block++;
      node->dn->info.i_next_alloc_goal++;
    }

  b = block;

  if (block < EXT2_NDIR_BLOCKS)
    return inode_getblk (node, block, create, 0, b, disk_block);

  block -= EXT2_NDIR_BLOCKS;
  if (block < addr_per_block)
    {
      err = inode_getblk (node, EXT2_IND_BLOCK, create, 1, b, &indir);
      if (!err)
	err = block_getblk (node, indir, block, create, 0, b, disk_block);
      return err;
    }

  block -= addr_per_block;
  if (block < addr_per_block * addr_per_block)
    {
      err = inode_getblk (node, EXT2_DIND_BLOCK, create, 1, b, &indir);
      if (!err)
	err = block_getblk (node, indir, block / addr_per_block, create, 1,
			    b, &indir);
      if (!err)
	err = block_getblk (node, indir, block & (addr_per_block - 1),
			    create, 0, b, disk_block);
      return err;
    }

  block -= addr_per_block * addr_per_block;
  err = inode_getblk (node, EXT2_TIND_BLOCK, create, 1, b, &indir);
  if (!err)
    err = block_getblk (node, indir, block / (addr_per_block * addr_per_block),
			create, 1, b, &indir);
  if (!err)
    err =
      block_getblk (node, indir,
		    (block / addr_per_block) & (addr_per_block - 1),
		    create, 1, b, &indir);
  if (!err)
    err = block_getblk (node, indir, block & (addr_per_block - 1), create, 0,
			b, disk_block);

  return err;
}
