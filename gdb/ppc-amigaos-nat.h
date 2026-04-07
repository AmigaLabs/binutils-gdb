/* Native-dependent code for PowerPC's running AmigaOS, for GDB.

   Copyright (C) 2013-2024 Free Software Foundation, Inc.

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

#ifndef PPC_AMIGAOS_NAT_H
#define PPC_AMIGAOS_NAT_H

#include <exec/ports.h>

#define PPC_AMIGAOS_SIZEOF_VRREGSET 532

/* AmigaOS SDK trap numbers from exec/interrupts.h (enTrapNumbers).
   ExceptionContext.Traptype uses these values, NOT raw PPC vector offsets. */
#define TRAP_BUS_ERROR              0x01000000 /* Bus error / machine check */
#define TRAP_DATA_SEGMENT           0x02000000 /* Data segment violation (DSI) */
#define TRAP_INST_SEGMENT           0x03000000 /* Instruction segment violation (ISI) */
#define TRAP_ALIGNMENT              0x04000000 /* Alignment violation */
#define TRAP_ILLEGAL_INSTRUCTION    0x05000000 /* Illegal instruction */
#define TRAP_PRIVILEGE_VIOLATION    0x06000000 /* Privilege violation */
#define TRAP_TRAP                   0x07000000 /* Trap instruction (breakpoint) */
#define TRAP_FPU                    0x08000000 /* Floating point (disabled/imprecise) */
#define TRAP_TRACE                  0x09000000 /* Single step trace */
#define TRAP_DATA_BREAKPOINT        0x0a000000 /* Data breakpoint (DABR) */
#define TRAP_INST_BREAKPOINT        0x0b000000 /* Instruction breakpoint */
#define TRAP_PERFORMANCE            0x0c000000 /* Performance monitor */
#define TRAP_THERMAL                0x0d000000 /* Thermal management */
#define TRAP_RESERVED1              0x0e000000 /* Reserved */
#define TRAP_ALTIVEC_ASSIST         0x0f000000 /* AltiVec assist */
#define TRAP_SMI                    0x10000000 /* System Management interrupt */

/* MSR Bits for Program exception sub-classification */
#define    MSR_TRACE_ENABLE           0x00000400
#define    EXC_FPE                    0x00100000
#define    EXC_ILLEGAL                0x00080000
#define    EXC_PRIV                   0x00040000
#define    EXC_TRAP                   0x00020000

/* Message sent from debugger hook to debugger to alert debugger
   of an event that happened */
struct debugger_message
{
	struct Message msg;
	struct Process *process;
	uint32 flags;
	uint32 signal;
	struct Library *library;
	void* seglist;
	int32 ReturnCode;
};

/* Possible debuger_message flags */
#define    DM_FLAGS_TASK_TERMINATED				0x00000001
#define    DM_FLAGS_TASK_ATTACHED				0x00000002
#define    DM_FLAGS_TASK_INTERRUPTED			0x00000004
#define	   DM_FLAGS_TASK_OPENLIB				0x00000008
#define	   DM_FLAGS_TASK_CLOSELIB				0x00000010
#define    DM_FLAGS_TASK_FINAL					0x10000000

#endif /* PPC_AMIGAOS_NAT_H */
