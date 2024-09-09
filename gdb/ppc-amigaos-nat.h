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

// Chapter Interrupt Reference union from different ppc32 cpus
#define TRAP_RESET 			0x0100 /* System reset */
#define TRAP_MCE   			0x0200 /* Machine check */
#define TRAP_DSI    		0x0300 /* Data storage */
#define TRAP_DSEGI   		0x0380 /* Data segment (Book III v2.01) */
#define TRAP_ISI     		0x0400 /* Instruction storage */
#define TRAP_ISEGI   		0x0480 /* Instruction segment (Book III v2.01)*/
#define TRAP_EXTERN   		0x0500 /* External Interrupt */
#define TRAP_ALIGN   		0x0600 /* Alignment */
#define TRAP_PROG    		0x0700 /* Program */
#define TRAP_FPU			0x0800 /* FPU Disabled */
#define TRAP_DEC			0x0900 /* Decrementer */
#define TRAP_RESERVEDA		0x0a00 /* Reserved (Book III v2.01)*/
#define TRAP_RESERVEDB		0x0b00 /* Reserved (Book III v2.01)*/
#define TRAP_SYSCALL		0x0c00 /* System call */
#define TRAP_TRACEI			0x0d00 /* Trace */
#define TRAP_FPA			0x0e00 /* Floating-point Assist */
#define TRAP_PMI     		0x0f00 /* Performance monitor (Book III v2.01)*/
#define TRAP_APU			0x0f20 /* APU Unavailble */
#define TRAP_PIT			0x1000 /* Programmable-interval timer (PIT) */
#define TRAP_FIT			0x1010 /* Fixed-interval timer (FIT) */
#define TRAP_WATCHDOG		0x1020 /* Watch Dog */
#define TRAP_DTBL			0x1100 /* Data TBL error */
#define TRAP_ITBL			0x1200 /* Instruction TBL error */
#define TRAP_DEBUG			0x2000 /* Debug */

/* MSR Bits */
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
};

/* Possible debuger_message flags */
#define    DM_FLAGS_TASK_TERMINATED            0x00000001
#define    DM_FLAGS_TASK_ATTACHED              0x00000002
#define    DM_FLAGS_TASK_INTERRUPTED           0x00000004
#define	   DM_FLAGS_TASK_OPENLIB               0x00000008
#define	   DM_FLAGS_TASK_CLOSELIB              0x00000010

#endif /* PPC_AMIGAOS_NAT_H */
