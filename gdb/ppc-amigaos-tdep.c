/* Target-dependent code for GDB, the GNU debugger.

   Copyright (C) 1986-2024 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "gdbcore.h"
#include "gdbarch.h"
#include "ppc-tdep.h"

static int
ppc_amigaos_has_shared_address_space (struct gdbarch *gdbarch)
{
	return true;
}

/* 1 to 1 copy from rs6000aix-tdp.c branch_dest , 
except removed AIX_TEXT_SEGMENT_BASE checks, 
removed byte_order */
static CORE_ADDR
branch_dest (struct regcache *regcache, int opcode, int instr,
	     CORE_ADDR pc, CORE_ADDR safety)
{
  struct gdbarch *gdbarch = regcache->arch ();
  ppc_gdbarch_tdep *tdep = gdbarch_tdep<ppc_gdbarch_tdep> (gdbarch);
  CORE_ADDR dest;
  int immediate;
  int absolute;
  int ext_op;

  absolute = (int) ((instr >> 1) & 1);

  switch (opcode)
    {
    case 18:
      immediate = ((instr & ~3) << 6) >> 6;	/* br unconditional */
      if (absolute)
	dest = immediate;
      else
	dest = pc + immediate;
      break;

    case 16:
      immediate = ((instr & ~3) << 16) >> 16;	/* br conditional */
      if (absolute)
	dest = immediate;
      else
	dest = pc + immediate;
      break;

    case 19:
      ext_op = (instr >> 1) & 0x3ff;

      if (ext_op == 16)		/* br conditional register */
	  dest = regcache_raw_get_unsigned (regcache, tdep->ppc_lr_regnum) & ~3;
      else if (ext_op == 528)	/* br cond to count reg */
	  dest = regcache_raw_get_unsigned (regcache,
					    tdep->ppc_ctr_regnum) & ~3;
      else
	return -1;
      break;

    default:
      return -1;
    }
  return dest;
}

/* 1 to 1 copy from rs6000aix-tdp.c rs6000_software_single_step */
static std::vector<CORE_ADDR>
ppc_amigaos_software_single_step (struct regcache *regcache)
{
  struct gdbarch *gdbarch = regcache->arch ();
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  int ii, insn;
  CORE_ADDR loc;
  CORE_ADDR breaks[2];
  int opcode;

  loc = regcache_read_pc (regcache);

  insn = read_memory_integer (loc, 4, byte_order);

  std::vector<CORE_ADDR> next_pcs = ppc_deal_with_atomic_sequence (regcache);
  if (!next_pcs.empty ())
    return next_pcs;
  
  breaks[0] = loc + PPC_INSN_SIZE;
  opcode = insn >> 26;
  breaks[1] = branch_dest (regcache, opcode, insn, loc, breaks[0]);

  /* Don't put two breakpoints on the same address.  */
  if (breaks[1] == breaks[0])
    breaks[1] = -1;

  for (ii = 0; ii < 2; ++ii)
    {
      /* ignore invalid breakpoint.  */
      if (breaks[ii] == -1)
	continue;

      next_pcs.push_back (breaks[ii]);
    }

  errno = 0;			/* FIXME, don't ignore errors!  */
  /* What errors?  {read,write}_memory call error().  */
  return next_pcs;
}

static void
ppc_amigaos_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
	/* Canonical paths on this target look like `SYS:Utilities/Clock', for example.  */
	set_gdbarch_has_amiga_based_file_system (gdbarch, 1);

	/* Everything runs in the same address space, but might have a priveta adresse area */
	set_gdbarch_has_shared_address_space (gdbarch, ppc_amigaos_has_shared_address_space);

	// PT_STEP not supported so, need to simulate it, like rs6000-aix
	set_gdbarch_software_single_step (gdbarch, ppc_amigaos_software_single_step);
	/* Displaced stepping is currently not supported in combination with
		software single-stepping.  These override the values set by
		rs6000_gdbarch_init.  */
	set_gdbarch_displaced_step_copy_insn (gdbarch, NULL);
	set_gdbarch_displaced_step_fixup (gdbarch, NULL);
	set_gdbarch_displaced_step_prepare (gdbarch, NULL);
	set_gdbarch_displaced_step_finish (gdbarch, NULL);

  	// Traget bfd name, seems to be only needed for message/debug output
	set_gdbarch_gcore_bfd_target (gdbarch, "elf32-powerpc-amigaos");
}

void _initialize_ppc_amigaos_tdep ();
void
_initialize_ppc_amigaos_tdep ()
{
	gdbarch_register_osabi (bfd_arch_rs6000, 0, GDB_OSABI_AMIGAOS, ppc_amigaos_init_abi);
	gdbarch_register_osabi (bfd_arch_powerpc, 0, GDB_OSABI_AMIGAOS, ppc_amigaos_init_abi);
}
