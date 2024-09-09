
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

#include "ppc-amigaos-nat.h"
#include "defs.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "symtab.h"
#include "exec.h"
#include "inferior.h"
#include "regset.h"
#include "regcache.h"
#include "inf-child.h"
#include "ppc-tdep.h"
#include "gdbsupport/ptid.h"
#include "gdbsupport/gdb_wait.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/elf.h>

#include <exec/exec.h>
#include <exec/execbase.h>
#include <exec/exectags.h>
#include <exec/tasks.h>
#include <exec/interrupts.h>

#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>


struct regcache_map_entry ppc_amigaos_vrregmap[] =
{
	{ 1,	PPC_VSCR_REGNUM,	16 },
	{ 32,	PPC_VR0_REGNUM,		16 },
	{ 1,	PPC_VRSAVE_REGNUM,	 4 },
	{ 0 }
};

const struct regset ppc_amigaos_vrregset = 
{
	ppc_amigaos_vrregmap,
	regcache_supply_regset,
	regcache_collect_regset
};

// From clib4 , bucket list to clear up 
extern struct Library *ElfBase;
extern struct ElfIFace *IElf;

struct DebugIFace *IDebug = NULL;
struct MMUIFace *IMMU = NULL;

struct amigaos_debug_hook_data
{
    struct Process		*current_process;
    struct Task			*debugger_task;
    struct MsgPort		*debugger_port;
};

/* The type of Message sent by IDebug->AddDebugHook () */
struct KernelDebugMessage
{
  uint32 type;
  union
  {
    struct ExceptionContext *context;
    struct Library *library;
  } message;
};

static VOID amigaos_debug_suspend ( struct Hook *amigaos_debug_hook );
static ULONG amigaos_debug_callback (struct Hook *, struct Task *, struct KernelDebugMessage *);
static int trap_to_signal(struct ExceptionContext *context, uint32 flags);
void ppc_amigaos_relocate_sections (const char *exec_file,BPTR exec_seglist);

#define MAX_DEBUG_MESSAGES 20

class ppc_amigaos_nat_target : public inf_child_target
{
private:

	struct Hook *					amigaos_debug_hook;
	void *							amigaos_debug_messages_storage = 0;
	struct List	*					amigaos_debug_messages_list;

public:

	struct amigaos_debug_hook_data	amigaos_debug_hook_data;

	/**
	 * Get an empty message and initialize it.
	 * @return
	 */
	struct debugger_message *
	alloc_message ( struct Process *process )
	{
		struct debugger_message *message = (struct debugger_message *)IExec->RemHead(amigaos_debug_messages_list);

		message->msg.mn_Node.ln_Type = NT_MESSAGE;
		message->msg.mn_Node.ln_Name = NULL;
		message->msg.mn_ReplyPort = NULL;
		message->msg.mn_Length = sizeof(struct debugger_message);
		message->process = process;

		return message;
	}

	/**
	 * Return a message to the pool. Note that we disable here so that we're not
	 * interrupted. Can't use semaphores because get_msg_packet is called during an
	 * exception.
	 *
	 * @param msg
	 */
	void
	free_message( struct debugger_message *message )
	{
		if (message)
		{
			IExec->AddTail(amigaos_debug_messages_list, (struct Node *)message);
		}
	}
	
	ppc_amigaos_nat_target ();
	~ppc_amigaos_nat_target () override;
	
	ptid_t wait (ptid_t, struct target_waitstatus *, target_wait_flags) override;

	void fetch_registers (struct regcache *, int) override;	
	void store_registers (struct regcache *, int) override;

    enum target_xfer_status xfer_partial (enum target_object object,const char *annex,gdb_byte *readbuf,const gdb_byte *writebuf,ULONGEST offset, ULONGEST len,ULONGEST *xfered_len) override;

	void attach (const char *, int) override;

	bool attach_no_wait () override
	{
		return true;
	}

	/*
	void post_attach (int pid) override
	{
		printf( "[GDB] %s (pid: %d)\n",__func__,pid );
	}
	
	void detach (inferior *inf, int from_tty) override
	{
		printf( "[GDB] %s ( inferior: %p, from_tty: %d )\n",__func__,inf,from_tty );
	}
	*/

	void create_inferior (const char *, const std::string &,char **, int) override;

	/*
	void mourn_inferior() override
	{
		printf( "[GDB] %s ()\n",__func__ );
	}
	*/
	
	// TODO: void prepare_to_store (regcache *regs) override;
	
	void resume (ptid_t ptid,int step,enum gdb_signal signal) override
	{
		struct Task *task = (struct Task *)(ptid == minus_one_ptid ? inferior_ptid.pid () : ptid.pid ());
		
		IExec->DebugPrintF("[GDB] %s ( step: %d, gdb_signal: %d, Task: %p  )\n",__func__,step,signal,task);

		IExec->RestartTask (task,0);
	}

	/*
	void kill () override
	{
		printf( "[GDB] %s ()\n",__func__ );
	}
	*/
	
	/*
	std::string pid_to_str (ptid_t ptid) override
	{
		IExec->DebugPrintF ("[GDB] %s Entering\n",__func__);
		
		/ *
		if( ptid != minus_one_ptid )
		{
			struct Task *task = (struct Task *)ptid.pid();

			IExec->Forbid();
			// ML: Should actually check that Task address is still a valid Task!?
			std::string str = string_printf (_("Task '%s' @ %s"), task->tc_Node.ln_Name ,phex ((ULONGEST)task, sizeof (void *)));
			IExec->Permit();

			return str;
		}
		* /

	  return normal_pid_to_str (ptid);
	}
	*/
	
	// TODO: char *pid_to_exec_file (int pid) override;
	
	/*
	bool info_proc (const char *args, enum info_proc_what what) override 
	{
		IExec->DebugPrintF("[GDB] %s (false)\n",__func__);
		return false;
	}
	*/
};

ppc_amigaos_nat_target::ppc_amigaos_nat_target ()
{
	ElfBase = IExec->OpenLibrary ("elf.library",0);
	if (!ElfBase)
	{
		error ("Can't open elf.library. How did you run *this* program ?\n");
	}

	IElf = (struct ElfIFace *)IExec->GetInterface (ElfBase,"main",1,0);
	if (!IElf)
	{
		IExec->CloseLibrary (ElfBase);

		ElfBase = NULL;

		error ("Can't get elf.library::main\n");
	}

	IMMU = (struct MMUIFace *)IExec->GetInterface ((struct Library *)SysBase,"mmu",1,0 );
	if (!IMMU)
	{
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IElf = NULL;
		ElfBase = NULL;

		error ("Can't get MMU access\n");
	}

	IDebug = (struct DebugIFace *)IExec->GetInterface ((struct Library *)SysBase,"debug",1,0 );
	if (!IDebug)
	{
		IExec->DropInterface ((struct Interface *)IMMU);
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IMMU = NULL;
		IElf = NULL;
		ElfBase = NULL;

		error ("Can't find kernel's debugger interface\n");
	}

	amigaos_debug_hook_data.debugger_port = (struct MsgPort *)IExec->AllocSysObjectTags (ASOT_PORT, ASOPORT_Name, "GDB", TAG_DONE);
	if (!amigaos_debug_hook_data.debugger_port)
	{
		IExec->DropInterface ((struct Interface *)IDebug);
		IExec->DropInterface ((struct Interface *)IMMU);
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IDebug = NULL;
		IMMU = NULL;
		IElf = NULL;
		ElfBase = NULL;

		error ("Can't allocate message port\n");
	}

	amigaos_debug_hook_data.current_process	= 0;
	amigaos_debug_hook_data.debugger_task	= IExec->FindTask ( NULL );

	amigaos_debug_hook = (struct Hook *)IExec->AllocSysObjectTags ( ASOT_HOOK, 
		ASOHOOK_Entry,	(HOOKFUNC)amigaos_debug_callback,
		ASOHOOK_Data,	(APTR)this,
		TAG_DONE);
	if (!amigaos_debug_hook)
	{
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook_data.debugger_port);
		IExec->DropInterface ((struct Interface *)IDebug);
		IExec->DropInterface ((struct Interface *)IMMU);
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IDebug = NULL;
		IMMU = NULL;
		IElf = NULL;
		ElfBase = NULL;

		error ("Can't allocate debugger hook\n");
	}

	if (!(amigaos_debug_messages_storage = IExec->AllocVecTags (MAX_DEBUG_MESSAGES * sizeof(struct debugger_message), AVT_Type, MEMF_SHARED, TAG_DONE)))
	{
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook);
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook_data.debugger_port);
		IExec->DropInterface ((struct Interface *)IDebug);
		IExec->DropInterface ((struct Interface *)IMMU);
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IDebug = NULL;
		IMMU = NULL;
		IElf = NULL;
		ElfBase = NULL;
	
	
		error ("Can't allocate memory for messages\n");
	}

	amigaos_debug_messages_list = (struct List *)IExec->AllocSysObjectTags ( ASOT_LIST, TAG_DONE);
	if (!amigaos_debug_messages_list)
	{
		IExec->FreeVec(amigaos_debug_messages_storage);
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook);
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook_data.debugger_port);
		IExec->DropInterface ((struct Interface *)IDebug);
		IExec->DropInterface ((struct Interface *)IMMU);
		IExec->DropInterface ((struct Interface *)IElf);
		IExec->CloseLibrary (ElfBase);

		IDebug = NULL;
		IMMU = NULL;
		IElf = NULL;
		ElfBase = NULL;

		error ("Can't allocate list for debug message\n");
	}

	struct debugger_message *msg = (struct debugger_message *)amigaos_debug_messages_storage;
	for (int i = 0; i < MAX_DEBUG_MESSAGES; i++)
	{
		IExec->AddHead( amigaos_debug_messages_list, (struct Node *)msg);
		msg++;
	}
}

ppc_amigaos_nat_target::~ppc_amigaos_nat_target ()
{
	/* Clear the debug hook (necessary to avoid the shell reusing it) */ 
	IDebug->AddDebugHook ((struct Task *)amigaos_debug_hook_data.current_process,NULL );

	/* Free pending messages and port */
	while (struct debugger_message *message = (struct debugger_message *)IExec->GetMsg (amigaos_debug_hook_data.debugger_port))
		free_message (message);

	if (amigaos_debug_messages_list)
	{
		IExec->FreeSysObject (ASOT_LIST,amigaos_debug_messages_list);
	}

	if (amigaos_debug_messages_storage)
	{
		IExec->FreeVec(amigaos_debug_messages_storage);
	}

	if (amigaos_debug_hook_data.debugger_port)
	{
		IExec->FreeSysObject (ASOT_PORT,amigaos_debug_hook_data.debugger_port);
	}
	amigaos_debug_hook_data.debugger_port = NULL;

	if (amigaos_debug_hook)
	{
		IExec->FreeSysObject (ASOT_HOOK,amigaos_debug_hook);
	}
	amigaos_debug_hook = NULL;

	if (IElf)
	{
		IExec->DropInterface ((struct Interface *)IElf);
	}
	IElf = NULL;

	if (ElfBase)
	{
		IExec->CloseLibrary (ElfBase);
	}
	ElfBase = NULL;

	if (IMMU)
	{
		IExec->DropInterface ((struct Interface *)IMMU);
	}
	IMMU = NULL;

	if (IDebug)
	{
		IExec->DropInterface ((struct Interface *)IDebug);
	}
	IDebug = NULL;
}

ptid_t
ppc_amigaos_nat_target::wait (ptid_t ptid, struct target_waitstatus *ourstatus,target_wait_flags options)
{
	struct Process *process = (ptid == minus_one_ptid ? amigaos_debug_hook_data.current_process : (struct Process *)ptid.pid());

	if( ptid == minus_one_ptid )
		ptid = ptid_t ((int)amigaos_debug_hook_data.current_process);

	while( 1 )
	{
		uint32 signal = IExec->Wait (SIGBREAKF_CTRL_D|SIGBREAKF_CTRL_C|1<<amigaos_debug_hook_data.debugger_port->mp_SigBit);

		if( ( signal & SIGBREAKF_CTRL_D ) == SIGBREAKF_CTRL_D )
		{
			IExec->DebugPrintF("[GDB] %s received SIGBREAKF_CTRL_D\n",__func__);

			ourstatus->set_exited (0);

			return ptid;
		}

		if( ( signal & SIGBREAKF_CTRL_C ) == SIGBREAKF_CTRL_C )
		{
			IExec->DebugPrintF("[GDB] %s received SIGBREAKF_CTRL_C\n",__func__);

			IExec->SuspendTask ((struct Task *)process,0);

			ourstatus->set_stopped (GDB_SIGNAL_TRAP);

			return ptid;
		}

		while (struct Message *message = IExec->GetMsg (amigaos_debug_hook_data.debugger_port) ) 		
		{
			IExec->DebugPrintF("[GDB] %s received message: %p\n",__func__,message);

			if( message->mn_Node.ln_Name != NULL && strcmp (message->mn_Node.ln_Name,"DeathMessage") == 0)
			{
				struct DeathMessage *deathMesage = (struct DeathMessage *)message;
				
				IExec->DebugPrintF("[GDB] %s received SIGB_CHILD with dos return: %ld\n",__func__,deathMesage->dm_ReturnCode);

				ourstatus->set_exited (deathMesage->dm_ReturnCode);

				IExec->FreeVec (deathMesage);

				return ptid;
			}
			else 
			{
				struct debugger_message *debuggerMessage = (struct debugger_message *)message;
				if( debuggerMessage->signal == -1 )
				{
					switch( debuggerMessage->flags )
					{
						case DM_FLAGS_TASK_OPENLIB:
						{
							IExec->DebugPrintF("[GDB] %s received task open library\n",__func__);

							break;
						}
						case DM_FLAGS_TASK_CLOSELIB:
						{
							IExec->DebugPrintF("[GDB] %s received task close library\n",__func__);

							break;
						}
						default:
						{
							IExec->DebugPrintF("[GDB] %s received unknown flags for signal -1 from callback %ld\n",__func__,debuggerMessage->flags);
		
							break;
						}
					}


					free_message (debuggerMessage);
				}
				else
				{
					IExec->DebugPrintF("[GDB] %s Inferior (%p) signaled : '%s'\n",__func__,process,gdb_signal_to_name ((enum gdb_signal)debuggerMessage->signal));

					switch (debuggerMessage->signal)
					{
						case GDB_SIGNAL_CHLD:
						{
							ourstatus->set_signalled (GDB_SIGNAL_0);

							break;
						}
						case GDB_SIGNAL_QUIT:
						{
							ourstatus->set_signalled (GDB_SIGNAL_QUIT);

							break;
						}
						case GDB_SIGNAL_TRAP:
						{
							ourstatus->set_stopped (GDB_SIGNAL_TRAP);

							break;
						}
						case GDB_SIGNAL_SEGV:
						case GDB_SIGNAL_BUS:
						case GDB_SIGNAL_INT:
						case GDB_SIGNAL_FPE:
						case GDB_SIGNAL_ILL:
						case GDB_SIGNAL_ALRM:					
						{					
							ourstatus->set_stopped (GDB_SIGNAL_0);

							break;
						}
						default:
						{
							IExec->DebugPrintF("[GDB] %s received unknown signal from callback %ld\n",__func__,debuggerMessage->signal);

							break;
						}
					}

					free_message (debuggerMessage);
					
					return ptid;
				}
			}
		}
	}

	return ptid_t::make_minus_one ();
}

/* Fetch register REGNO from the inferior.  */
void 
ppc_amigaos_nat_target::fetch_registers (struct regcache *regcache, int regno) 
{
	struct gdbarch *gdbarch = regcache->arch ();
	ppc_gdbarch_tdep *tdep = gdbarch_tdep<ppc_gdbarch_tdep> (gdbarch);	
	struct Task *task = (struct Task *)regcache->ptid().pid();

	IExec->DebugPrintF("[GDB] %s ( regcache: %p, regno: %d (%s), task: %p)\n",__func__,regcache,regno,gdbarch_register_name( gdbarch,regno ),task);

	struct ExceptionContext context;
	IDebug->ReadTaskContext( task,&context,RTCF_INFO | RTCF_SPECIAL | RTCF_STATE | RTCF_GENERAL | RTCF_FPU | RTCF_VECTOR );

	if( regno == -1 )
	{
		for (int i = 0; i < 31; i++)
			regcache->raw_supply (regno, (void*)&context.gpr[i]);

		for (int i = 0; i < 31; i++)
			regcache->raw_supply (regno, (void*)&context.fpr[i]);

		regcache->raw_supply (gdbarch_pc_regnum (gdbarch), (void *)&context.ip);
		regcache->raw_supply (tdep->ppc_ps_regnum, (void *)&context.msr);
		regcache->raw_supply (tdep->ppc_cr_regnum, (void *)&context.cr);
		regcache->raw_supply (tdep->ppc_lr_regnum, (void *)&context.lr);
		regcache->raw_supply (tdep->ppc_ctr_regnum, (void *)&context.ctr);
		regcache->raw_supply (tdep->ppc_xer_regnum, (void *)&context.xer);
		regcache->raw_supply (tdep->ppc_fpscr_regnum, (void *)&context.fpscr);			

		if (tdep->ppc_vr0_regnum != -1 && tdep->ppc_vrsave_regnum != -1)
		{
			ppc_amigaos_vrregset.supply_regset( &ppc_amigaos_vrregset,regcache,regno,(void *)&context.vscr,PPC_AMIGAOS_SIZEOF_VRREGSET );
		}
	}
	else 
	{
		if (regno == gdbarch_pc_regnum (gdbarch) )
		{			
			regcache->raw_supply (regno, (void*)&context.ip);
		}
		else if (regno >= 0 && regno <= 31) 
		{
			regcache->raw_supply (regno, (void*)&context.gpr[regno]);
		}
		else if (altivec_register_p (gdbarch, regno))
		{
			ppc_amigaos_vrregset.supply_regset( &ppc_amigaos_vrregset,regcache,regno,(void *)&context.vscr,PPC_AMIGAOS_SIZEOF_VRREGSET );
		}
		else if (regno >= 32 && regno <= 64)
			regcache->raw_supply (regno, (void*)&context.fpr[regno]);
		else if (regno == tdep->ppc_ps_regnum)
			regcache->raw_supply (regno, (void *)&context.msr);
		else if (regno == tdep->ppc_cr_regnum)
			regcache->raw_supply (tdep->ppc_cr_regnum, (void *)&context.cr);
		else if (regno == tdep->ppc_lr_regnum)
			regcache->raw_supply (tdep->ppc_lr_regnum, (void *)&context.lr);
		else if (regno == tdep->ppc_ctr_regnum) 
			regcache->raw_supply (tdep->ppc_ctr_regnum, (void *)&context.ctr);
		else if (regno == tdep->ppc_xer_regnum)
			regcache->raw_supply (tdep->ppc_xer_regnum, (void *)&context.xer);
		else if (regno == tdep->ppc_fpscr_regnum)
			regcache->raw_supply (tdep->ppc_fpscr_regnum, (void *)&context.fpscr);		
		else if (regno == tdep->ppc_vr0_regnum)
			regcache->raw_supply (tdep->ppc_vr0_regnum, (void *)&context.vr );		
		else if (regno == tdep->ppc_vrsave_regnum)
			regcache->raw_supply (tdep->ppc_vrsave_regnum, (void *)&context.vrsave );		
		else
		{
			internal_error (_("fetch_registers: unexpected register: '%s'"),gdbarch_register_name ( gdbarch,regno ));
		}
	}
}

void
ppc_amigaos_nat_target::store_registers (struct regcache *regcache, int regno)
{
	printf( "[GDB] %s Todo ( regcache: %p, regno: %d)\n",__func__,regcache,regno );
}

enum target_xfer_status
ppc_amigaos_nat_target::xfer_partial (enum target_object object,const char *annex, gdb_byte *readbuf,const gdb_byte *writebuf,ULONGEST offset, ULONGEST len, ULONGEST *xfered_len)
{
	IExec->DebugPrintF ( string_printf (_("[GDB] %s called for memory tranfser at address: 0x%s for %s bytes (readbuf: %p, writebuf: %p, annex: '%s', object: %d )\n"),__func__,phex (offset, sizeof (offset)),pulongest (len), readbuf, writebuf, annex, object).c_str());

	switch (object)
	{
		case TARGET_OBJECT_MEMORY:
		{
			if (offset == 0) 
			{
				// ML: Helps to unwind farme correctly
				return TARGET_XFER_E_IO;
			}
			else
			{
				APTR user_stack = IExec->SuperState();

				ULONG currentAttrs = IMMU->GetMemoryAttrs( (APTR)offset,0 );
				IMMU->SetMemoryAttrs ( (APTR)offset,len,MEMATTRF_READ_WRITE );

				if (readbuf) 
				{
					IExec->CopyMem( (APTR)offset,(APTR)readbuf,len );
				}
				else // if(writebuf)
				{
					IExec->CopyMem( (APTR)writebuf,(APTR)offset,len );
					IExec->CacheClearE( (APTR)offset,len,CACRF_ClearI );
				}

				IMMU->SetMemoryAttrs( (APTR)offset,len,currentAttrs );

				if (user_stack)
				{
					IExec->UserState( user_stack );
				}
			}

			*xfered_len = len;

			IExec->DebugPrintF ( string_printf (_("[GDB] %s tansferfed %s bytes\n"),__func__,pulongest (*xfered_len)).c_str());

			return TARGET_XFER_OK;			
		}
		break;

		case TARGET_OBJECT_LIBRARIES:
		{
			IExec->DebugPrintF("[GDB] %s tansferfed object library '%s' failed, aka not supported yet\n",__func__,annex);

			return TARGET_XFER_E_IO;			
		}
		break;

		default:
			if (beneath()) 
			{
				IExec->DebugPrintF("[GDB] %s tansferfed delegated to beneath for target_object %d\n",__func__,object);

				return this->beneath ()->xfer_partial (object,annex,readbuf,writebuf,offset,len,xfered_len);
			}
			/* This can happen when requesting the transfer of unsupported
			 objects before a program has been started (and therefore
			 with the current_target having no target beneath).  */
	}

	return TARGET_XFER_E_IO;
}

void
ppc_amigaos_nat_target::attach (const char *args, int from_tty)
{
	printf( "[GDB] %s ( args: '%s', from_tty: %d )\n",__func__,args,from_tty );
}

void
ppc_amigaos_relocate_sections (const char *exec_file,BPTR exec_seglist) 
{
	// To debug this stuff, enableing elf debug out with 'SetENV ELF.debug 1' helps alot on thr target
	Elf32_Handle exec_elfhandle = 0;
	if( 1 == IDOS->GetSegListInfoTags( exec_seglist,
		GSLI_ElfHandle,		&exec_elfhandle,
		TAG_DONE) )
	{
		Elf32_Handle exec_opendelf = IElf->OpenElfTags(
			OET_ElfHandle,			exec_elfhandle,
			OET_ReadOnlyCopy,		TRUE,
			TAG_DONE );
		if( exec_opendelf ) 
		{
			if( current_program_space->symfile_object_file ) 
			{
				struct objfile *symfile = current_program_space->symfile_object_file;
				section_offsets offsets (symfile->section_offsets.size () );

				struct obj_section *osect;
				ALL_OBJFILE_OSECTIONS(symfile, osect)
				{
					struct bfd_section *section = osect->the_bfd_section;
					int osect_idx = osect - symfile->sections;
					
					void *address = IElf->GetSectionTags( exec_opendelf,
						GST_SectionName, section->name,
						TAG_DONE );
						
					if( address )
					{
						IExec->DebugPrintF ( string_printf (_("[GDB] On symfile_object relocated %d section '%s' from 0x%08lx to %p, size %ld, old offset: 0x%s\n"),section->index,section->name,section->vma,address,section->size,phex ( osect->addr(), sizeof (osect->addr()))).c_str());
						
						offsets[ osect_idx ] = (CORE_ADDR)address - osect->addr();
					}
				}

				objfile_relocate(symfile, offsets);
			}
			else if( current_program_space->exec_bfd() )
			{
				/* Go through all GDB sections, and make sure they are loaded and relocated */
				for (asection *section : gdb_bfd_sections (current_program_space->exec_bfd ())) {

					void *address = IElf->GetSectionTags( exec_opendelf,
						GST_SectionName, section->name,
						TAG_DONE );
								
					if( address )
					{
						printf ( "[GDB] On exec_bfd relocated %d section '%s' from %08lx to %p, size %ld\n",section->index,section->name,section->vma,address,section->size);
						
						exec_set_section_address( exec_file,section->index,(CORE_ADDR)address );							
					}
				}
			}

			IElf->CloseElfTags( exec_opendelf,
				CET_FreeUnneeded,		TRUE,
				TAG_DONE );
		}
	}
}

/* Start a new inferior AmigaOS DOS process.  EXEC_FILE is the file to
   run, ALLARGS is a string containing the arguments to the program.
   ENV is the environment vector to pass.  */
void ppc_amigaos_nat_target::create_inferior (const char *exec_file,const std::string &allargs,char **env, int from_tty)
{
	inferior *inf = current_inferior ();
	if( !inf )
	{
		error ("No current inferior present" );
	}

	/* If no exec file handed to us, get it from the exec-file command -- with
	 a good, common error message if none is specified.  */
	if (exec_file == 0)
	{
	    exec_file = get_exec_file (1);
	}

	BPTR exec_seglist = IDOS->LoadSeg( exec_file );
	if( ! exec_seglist )
	{
		error ("'%s': not an executable file\n",exec_file );
	}

	BPTR exec_home = ZERO;
	BPTR exec_lock = IDOS->Lock( exec_file,SHARED_LOCK );
	if( exec_lock )
	{
		exec_home = IDOS->ParentDir( exec_lock );

		IDOS->UnLock( exec_lock );
	}

	struct DeathMessage *dmsg = (struct DeathMessage *)IExec->AllocVecTags( sizeof( struct DeathMessage ),AVT_Type, MEMF_SHARED, TAG_DONE );
	if( dmsg == NULL )
	{
		error ("Can't allocate memory for death message\n");
	}

	dmsg->dm_Msg.mn_ReplyPort = amigaos_debug_hook_data.debugger_port;
	dmsg->dm_Msg.mn_Length = sizeof( struct DeathMessage );
	dmsg->dm_Msg.mn_Node.ln_Name = (char*)"DeathMessage";

	amigaos_debug_hook_data.current_process = IDOS->CreateNewProcTags(
			NP_Seglist,										exec_seglist,
			NP_FreeSeglist,									FALSE,
			NP_EntryCode,									amigaos_debug_suspend,
			NP_EntryData,									amigaos_debug_hook,
			NP_Child,										TRUE,
			NP_NotifyOnDeathMessage,						dmsg, // Signal parent with with prepared reply msg
			NP_Name,										lbasename( exec_file ),
			NP_CommandName,									lbasename( exec_file ),
			NP_Cli,											TRUE,
			NP_Arguments,									allargs.c_str(),
			NP_Input,										IDOS->Input(),
			NP_CloseInput,									FALSE,
			NP_Output,										IDOS->Output(),
			NP_CloseOutput,									FALSE,
			NP_Error,										IDOS->ErrorOutput(),
			NP_CloseError,									FALSE,
			(exec_home ? NP_ProgramDir: TAG_IGNORE),		exec_home,
			TAG_DONE
		);

	if (! amigaos_debug_hook_data.current_process)
	{
		error ("Can't create AmigaOS DOS process\n");
	}

	IDebug->AddDebugHook((struct Task *)amigaos_debug_hook_data.current_process,amigaos_debug_hook);

	inferior_ptid = ptid_t ((int)amigaos_debug_hook_data.current_process);
	inferior_appeared (inf,(int)amigaos_debug_hook_data.current_process);
	/* We have something that executes now.  We'll be running through
	 the shell at this point (if startup-with-shell is true), but the
	 pid shouldn't change.  */

	/* Do not change either targets above or the same target if already present.
	 The reason is the target stack is shared across multiple inferiors.  */
	inf->unpush_target(this);
	
	if(!inf->target_is_pushed(this))
		inf->push_target(this);

	thread_info *thr = add_thread (this, inferior_ptid);
	switch_to_thread (thr);

	clear_proceed_status (0);
	init_wait_for_inferior ();

	ppc_amigaos_relocate_sections (exec_file,exec_seglist);

	IExec->DebugPrintF("[GDB] %s inferior_ptid=0x%08x inf=%p thr=%p\n",__func__,inferior_ptid.pid(),inf,thr);

	//  ML: Don't do it here, because we need to keep the exec_segliots until the task has finished
	// IDOS->UnLoadSeg( exec_seglist );
}

static ppc_amigaos_nat_target the_ppc_amigaos_nat_target;

void _initialize_ppcamigaos_nat ();
void
_initialize_ppcamigaos_nat ()
{
	add_inf_child_target (&the_ppc_amigaos_nat_target);
}

VOID amigaos_debug_suspend( struct Hook *amigaos_debug_hook ) 
{
	struct Task *current = IExec->FindTask (NULL);

	IExec->DebugPrintF("[GDB] %s inferiorer %p started by kernel, suspending myself and installing debug hook: %p\n",__func__,current,amigaos_debug_hook);
	
	IExec->SuspendTask (current,0);

	IExec->DebugPrintF("[GDB] %s inferiorer %p started by gdb\n",__func__,current);
}

ULONG amigaos_debug_callback (struct Hook *hook, struct Task *currentTask,struct KernelDebugMessage *dbgmsg )
{
	class ppc_amigaos_nat_target *ppc_amigaos_nat_target = (class ppc_amigaos_nat_target *)hook->h_Data;
	struct amigaos_debug_hook_data *data = &(ppc_amigaos_nat_target->amigaos_debug_hook_data);
	
	if( (struct Task *)data->current_process != currentTask )
	{
		IExec->DebugPrintF ("[GDB] Task: %p ('%s'), task NOT under our observation\n",currentTask,currentTask->tc_Node.ln_Name);

		return 0;
	}
	IExec->DebugPrintF ("[GDB] Task: %p ('%s'), task IS under our observation\n",currentTask,currentTask->tc_Node.ln_Name);


	switch( dbgmsg->type ) 
	{
		case DBHMT_EXCEPTION:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'),Exception ooccured (DBHMT_EXCEPTION)\n",currentTask,currentTask->tc_Node.ln_Name);

			struct debugger_message *message = ppc_amigaos_nat_target->alloc_message ((struct Process *)currentTask);
			message->flags	= 0;
			message->signal	= trap_to_signal( dbgmsg->message.context,message->flags );
			
			IExec->PutMsg (data->debugger_port,(struct Message *)message);

			return 1; // Suspend execution
		}
		case DBHMT_ADDTASK:
		{
			IExec->DebugPrintF("[GDB] Task: %p ('%s'), (DBHMT_ADDTASK), Task added\n",currentTask,currentTask->tc_Node.ln_Name);

			struct debugger_message *message = ppc_amigaos_nat_target->alloc_message ((struct Process *)currentTask);	
			message->flags	= DM_FLAGS_TASK_ATTACHED;
			message->signal	= -1;
			
			IExec->PutMsg (data->debugger_port,(struct Message *)message);

			break;
		}
		case DBHMT_REMTASK:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_REMTASK), Task removed\n",currentTask,currentTask->tc_Node.ln_Name);

			struct debugger_message *message = ppc_amigaos_nat_target->alloc_message ((struct Process *)currentTask);
			message->flags	= DM_FLAGS_TASK_TERMINATED;
			message->signal	= -1;
			
			IExec->PutMsg (data->debugger_port,(struct Message *)message);
			
			break;
		}
		case DBHMT_OPENLIB:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_OPENLIB), Task opened library '%s'\n",currentTask,currentTask->tc_Node.ln_Name,(char*)dbgmsg->message.library->lib_IdString);

			struct debugger_message *message = ppc_amigaos_nat_target->alloc_message ((struct Process *)currentTask);
			message->flags		= DM_FLAGS_TASK_OPENLIB;
			message->signal		= -1;
			message->library	= dbgmsg->message.library;
			
			IExec->PutMsg (data->debugger_port,(struct Message *)message);

			break;
		}
		case DBHMT_CLOSELIB:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_CLOSELIB), Task closed library '%s'\n",currentTask,currentTask->tc_Node.ln_Name,(char*)dbgmsg->message.library->lib_IdString);

			struct debugger_message *message = ppc_amigaos_nat_target->alloc_message ((struct Process *)currentTask);	
			message->flags		= DM_FLAGS_TASK_CLOSELIB;
			message->signal		= -1;
			message->library	= dbgmsg->message.library;

			IExec->PutMsg (data->debugger_port,(struct Message *)message);

			break;
		}
		case DBHMT_SHAREDOBJECTOPEN:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_SHAREDOBJECTOPEN), Task opened shared object '%s'\n",currentTask,currentTask->tc_Node.ln_Name,(char*)dbgmsg->message.library->lib_IdString);
			break;
		}
		case DBHMT_SHAREDOBJECTCLOSE:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_SHAREDOBJECTCLOSE), Task closed shared object '%s'\n",currentTask,currentTask->tc_Node.ln_Name,(char*)dbgmsg->message.library->lib_IdString);
			break;
		}
		default:
		{
			IExec->DebugPrintF ("[GDB] Task: %p ('%s'), (DBHMT_UNKNOWN), Task unknown message type %lu\n",currentTask,currentTask->tc_Node.ln_Name,dbgmsg->type);
		}
	}

	return 0; // Resume execution
}

static int
trap_to_signal(struct ExceptionContext *context, uint32 flags)
{
	IExec->DebugPrintF( "[GDB] trap_to_signal ( flags: 0x%lx )\n",flags );

	if (!context || (flags & DM_FLAGS_TASK_TERMINATED)) {
		IExec->DebugPrintF( "[GDB] Return GDB_SIGNAL_QUIT )\n" );
	
		return GDB_SIGNAL_QUIT;
	}

	IExec->DebugPrintF( "[GDB] traptype: 0x%lx\n",context->Traptype );

	switch (context->Traptype)
	{
	case TRAP_MCE:
	case TRAP_DSI:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_SEGV )\n" );
		return GDB_SIGNAL_SEGV;
	case TRAP_ISI:
	case TRAP_ALIGN:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_BUS )\n" );
		return GDB_SIGNAL_BUS;
	case TRAP_EXTERN:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_INT )\n" );
		return GDB_SIGNAL_INT;
	case TRAP_PROG: 
		if (context->msr & EXC_FPE) {
			IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_FPE )\n" );
			return GDB_SIGNAL_FPE;
		}
		else if (context->msr & EXC_ILLEGAL) {
			IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_ILL )\n" );
			return GDB_SIGNAL_ILL;
		}
		else if (context->msr & EXC_PRIV) {
			IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_ILL )\n" );
			return GDB_SIGNAL_ILL;
		}
		else {
			IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_TRAP )\n" );
			return GDB_SIGNAL_TRAP;
		}
	case TRAP_FPU:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_FPE )\n" );
		return GDB_SIGNAL_FPE;
	case TRAP_DEC:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_ALRM )\n" );
		return GDB_SIGNAL_ALRM;
	case TRAP_RESERVEDA:
	case TRAP_RESERVEDB:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_ILL )\n" );
		return GDB_SIGNAL_ILL;
	case TRAP_SYSCALL:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_CHLD )\n" );
		return GDB_SIGNAL_CHLD;
	case TRAP_TRACEI:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_TRAP )\n" );
		return GDB_SIGNAL_TRAP;
	case TRAP_FPA:
		IExec->DebugPrintF( "[GDB] Return ( GDB_SIGNAL_FPE )\n" );
		return GDB_SIGNAL_FPE;
	default:
		IExec->DebugPrintF( "[GDB] Return ( -1 )\n" );
		return -1;
	}
}