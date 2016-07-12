/* pdp8_cpu.c: PDP-8 CPU simulator

   Copyright (c) 1993-2013, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          central processor

   17-Sep-13    RMS     Fixed boot in wrong field problem (Dave Gesswein)
   28-Apr-07    RMS     Removed clock initialization
   30-Oct-06    RMS     Added idle and infinite loop detection
   30-Sep-06    RMS     Fixed SC value after DVI overflow (Don North)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   31-Dec-03    RMS     Fixed bug in set_cpu_hist
   13-Oct-03    RMS     Added instruction history
						Added TSC8-75 support (Bernhard Baehr)
   12-Mar-03    RMS     Added logical name support
   04-Oct-02    RMS     Revamped device dispatching, added device number support
   06-Jan-02    RMS     Added device enable/disable routines
   30-Dec-01    RMS     Added old PC queue
   16-Dec-01    RMS     Fixed bugs in EAE
   07-Dec-01    RMS     Revised to use new breakpoint package
   30-Nov-01    RMS     Added RL8A, extended SET/SHOW support
   16-Sep-01    RMS     Fixed bug in reset routine, added KL8A support
   10-Aug-01    RMS     Removed register from declarations
   17-Jul-01    RMS     Moved function prototype
   07-Jun-01    RMS     Fixed bug in JMS to non-existent memory
   25-Apr-01    RMS     Added device enable/disable support
   18-Mar-01    RMS     Added DF32 support
   05-Mar-01    RMS     Added clock calibration support
   15-Feb-01    RMS     Added DECtape support
   14-Apr-99    RMS     Changed t_addr to unsigned

   The register state for the PDP-8 is:

   AC<0:11>             accumulator
   MQ<0:11>             multiplier-quotient
   L                    link flag
   PC<0:11>             program counter
   IF<0:2>              instruction field
   IB<0:2>              instruction buffer
   DF<0:2>              data field
   UF                   user flag
   UB                   user buffer
   SF<0:6>              interrupt save field

   The PDP-8 has three instruction formats: memory reference, I/O transfer,
   and operate.  The memory reference format is:

	 0  1  2  3  4  5  6  7  8  9 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |   op   |in|zr|    page offset     |        memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+

   <0:2>        mnemonic        action

	000         AND             AC = AC & M[MA]
	001         TAD             L'AC = AC + M[MA]
	010         DCA             M[MA] = AC, AC = 0
	011         ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
	100         JMS             M[MA] = PC, PC = MA + 1
	101         JMP             PC = MA

   <3:4>        mode            action
	00  page zero               MA = IF'0'IR<5:11>
	01  current page            MA = IF'PC<0:4>'IR<5:11>
	10  indirect page zero      MA = xF'M[IF'0'IR<5:11>]
	11  indirect current page   MA = xF'M[IF'PC<0:4>'IR<5:11>]

   where x is D for AND, TAD, ISZ, DCA, and I for JMS, JMP.

   Memory reference instructions can access an address space of 32K words.
   The address space is divided into eight 4K word fields; each field is
   divided into thirty-two 128 word pages.  An instruction can directly
   address, via its 7b offset, locations 0-127 on page zero or on the current
   page.  All 32k words can be accessed via indirect addressing and the
   instruction and data field registers.  If an indirect address is in
   locations 0010-0017 of any field, the indirect address is incremented
   and rewritten to memory before use.

   The I/O transfer format is as follows:

	 0  1  2  3  4  5  6  7  8  9 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |   op   |      device     | pulse  |        I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified pulse to the
   specified I/O device.  The I/O device may take data from the AC,
   return data to the AC, initiate or cancel operations, or skip on
   status.

   The operate format is as follows:

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 0|  |  |  |  |  |  |  |  |        operate group 1
   +--+--+--+--+--+--+--+--+--+--+--+--+
				|  |  |  |  |  |  |  |
				|  |  |  |  |  |  |  +--- increment AC  3
				|  |  |  |  |  |  +--- rotate 1 or 2    4
				|  |  |  |  |  +--- rotate left         4
				|  |  |  |  +--- rotate right           4
				|  |  |  +--- complement L              2
				|  |  +--- complement AC                2
				|  +--- clear L                         1
				+-- clear AC                            1

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 1|  |  |  |  |  |  |  | 0|        operate group 2
   +--+--+--+--+--+--+--+--+--+--+--+--+
				|  |  |  |  |  |  |
				|  |  |  |  |  |  +--- halt             3
				|  |  |  |  |  +--- or switch register  3
				|  |  |  |  +--- reverse skip sense     1
				|  |  |  +--- skip on L != 0            1
				|  |  +--- skip on AC == 0              1
				|  +--- skip on AC < 0                  1
				+-- clear AC                            2

   +--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 1| 1| 1|  |  |  |  |  |  |  | 1|        operate group 3
   +--+--+--+--+--+--+--+--+--+--+--+--+
				|  |  |  | \______/
				|  |  |  |     |
				|  |  +--|-----+--- EAE command         3
				|  |     +--- AC -> MQ, 0 -> AC         2
				|  +--- MQ v AC --> AC                  2
				+-- clear AC                            1

  The operate instruction can be microprogrammed to perform operations
  on the AC, MQ, and link.

  This routine is the instruction decode routine for the PDP-8.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

		HALT instruction
		breakpoint encountered
		unimplemented instruction and stop_inst flag set
		I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by three parallel variables:

		dev_done        device done flags
		int_enable      interrupt enable flags
		int_req         interrupt requests

	  In addition, int_req contains the interrupt enable flag, the
	  CIF not pending flag, and the ION not pending flag.  If all
	  three of these flags are set, and at least one interrupt request
	  is set, then an interrupt occurs.

   3. Non-existent memory.  On the PDP-8, reads to non-existent memory
	  return zero, and writes are ignored.  In the simulator, the
	  largest possible memory is instantiated and initialized to zero.
	  Thus, only writes outside the current field (indirect writes) need
	  be checked against actual memory size.

   3. Adding I/O devices.  These modules must be modified:

		pdp8_defs.h     add device number and interrupt definitions
		pdp8_sys.c      add sim_devices table entry
*/

#include "pdp8_defs.h"
#ifdef USE_REALCONS
#include "realcons.h"
#include "realcons_console_pdp8i.h"
#endif


#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = MA
#define UNIT_V_NOEAE    (UNIT_V_UF)                     /* EAE absent */
#define UNIT_NOEAE      (1 << UNIT_V_NOEAE)
#define UNIT_V_MSIZE    (UNIT_V_UF + 1)                 /* dummy mask */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define OP_KSF          06031                           /* for idle */

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
	int32               pc;
	int32               ea;
	int16               ir;
	int16               opnd;
	int16               lac;
	int16               mq;
} InstHistory;

uint16 M[MAXMEMSIZE] = { 0 };                           /* main memory */
int32 saved_LAC = 0;                                    /* saved L'AC */
int32 saved_MQ = 0;                                     /* saved MQ */
int32 saved_PC = 0;                                     /* saved IF'PC */
int32 saved_DF = 0;                                     /* saved Data Field */
int32 IB = 0;                                           /* Instruction Buffer */
int32 SF = 0;                                           /* Save Field */
int32 emode = 0;                                        /* EAE mode */
int32 gtf = 0;                                          /* EAE gtf flag */
int32 SC = 0;                                           /* EAE shift count */
int32 UB = 0;                                           /* User mode Buffer */
int32 UF = 0;                                           /* User mode Flag */
int32 OSR = 0;                                          /* Switch Register */
int32 tsc_ir = 0;                                       /* TSC8-75 IR */
int32 tsc_pc = 0;                                       /* TSC8-75 PC */
int32 tsc_cdf = 0;                                      /* TSC8-75 CDF flag */
int32 tsc_enb = 0;                                      /* TSC8-75 enabled */
int16 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 dev_done = 0;                                     /* dev done flags */
int32 int_enable = INT_INIT_ENABLE;                     /* intr enables */
int32 int_req = 0;                                      /* intr requests */
int32 stop_inst = 0;                                    /* trap on ill inst */
int32(*dev_tab[DEV_MAX])(int32 IR, int32 dat);         /* device dispatch */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */

t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset(DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_bool build_dev_tab(void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA(NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATAD (PC, saved_PC, 15, "program counter") },
    { ORDATAD (AC, saved_LAC, 12, "accumulator") },
    { FLDATAD (L, saved_LAC, 12, "link") },
    { ORDATAD (MQ, saved_MQ, 12, "multiplier-quotient") },
    { ORDATAD (SR, OSR, 12, "front panel switches") },
    { GRDATAD (IF, saved_PC, 8, 3, 12, "instruction field") },
    { GRDATAD (DF, saved_DF, 8, 3, 12, "data field") },
    { GRDATAD (IB, IB, 8, 3, 12, "instruction field buffter") },
    { ORDATAD (SF, SF, 7, "save field") },
    { FLDATAD (UB, UB, 0, "user mode buffer") },
    { FLDATAD (UF, UF, 0, "user mode flag") },
    { ORDATAD (SC, SC, 5, "EAE shift counter") },
    { FLDATAD (GTF, gtf, 0, "EAE greater than flag") },
    { FLDATAD (EMODE, emode, 0, "EAE mode (0 = A, 1 = B)") },
    { FLDATAD (ION, int_req, INT_V_ION, "interrupt enable") },
    { FLDATAD (ION_DELAY, int_req, INT_V_NO_ION_PENDING, "interrupt enable delay for ION") },
    { FLDATAD (CIF_DELAY, int_req, INT_V_NO_CIF_PENDING, "interrupt enable delay for CIF") },
    { FLDATAD (PWR_INT, int_req, INT_V_PWR, "power fail interrupt") },
    { FLDATAD (UF_INT, int_req, INT_V_UF, "user mode violation interrupt") },
    { ORDATAD (INT, int_req, INT_V_ION+1, "interrupt pending flags"), REG_RO },
    { ORDATAD (DONE, dev_done, INT_V_DIRECT, "device done flags"), REG_RO },
    { ORDATAD (ENABLE, int_enable, INT_V_DIRECT, "device interrupt enable flags"), REG_RO },
    { BRDATAD (PCQ, pcq, 8, 15, PCQ_SIZE, "PC prior to last JMP, JMS, or interrupt;                                        most recent PC change first"), REG_RO+REG_CIRC },
	{ ORDATA(PCQP, pcq_p, 6), REG_HRO },
    { FLDATAD (STOP_INST, stop_inst, 0, "stop on undefined instruction") },
    { ORDATAD (WRU, sim_int_char, 8, "interrupt character") },
	{ NULL }
};

MTAB cpu_mod[] = {
	{ UNIT_NOEAE, UNIT_NOEAE, "no EAE", "NOEAE", NULL },
	{ UNIT_NOEAE, 0, "EAE", "EAE", NULL },
	{ MTAB_XTD | MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
	{ MTAB_XTD | MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
	{ UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
	{ UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
	{ UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
	{ UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
	{ UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
	{ UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
	{ UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
	{ UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
	{ MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
	  &cpu_set_hist, &cpu_show_hist },
	{ 0 }
};

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 15, 1, 8, 12,
	&cpu_ex, &cpu_dep, &cpu_reset,
	NULL, NULL, NULL,
	NULL, 0
};

#ifdef USE_REALCONS
// Extended cpu state for panel logic,
// State of MAJORSTATE signals and some flip-flops is added.
// 
// 1. state for all cpu's in scp.c
extern	t_addr realcons_memory_address_register; // memory address
extern 	t_value realcons_memory_data_register; // memory data
extern 	int realcons_console_halt; // 1: CPU halted by realcons console

// 2. state extension for PDP8, initialize in cpu_reset()

// bit mask for singals indicating last decoded and executed instruction, see REALCONS_PDP8I_INSTRUCTION_DECODE_*
int32 realcons_instruction_decode;

// Signals set for the major state indicator of an instruction
// "One of these indicates that the processor is currently performing or
// has performed a certain cycle."
// major states: SimH cannot single step through instruction sub cycles.
// so all cycles of current instructions are ORed together and shown
// simulataneously at isntruction end.
// See Maintenance Manual PDP-8I Volume I (Jul 1969, DEC-8I-HR1A-D), Chapter 4.7ff
int32 realcons_majorstate_curinstr; // all states of current instruction
int32 realcons_majorstate_last; // last active state
#define MAJORSTATE_CLEAR	do { realcons_majorstate_curinstr = realcons_majorstate_last = 0; } while (0)
#define MAJORSTATE_ENTER(state_bit) do { \
	realcons_majorstate_curinstr |= state_bit ;		\
	realcons_majorstate_last = state_bit	;			\
} while (0)
#define STATE_FETCH			REALCONS_PDP8I_MAJORSTATE_FETCH			
#define STATE_EXECUTE		REALCONS_PDP8I_MAJORSTATE_EXECUTE		
#define STATE_DEFER			REALCONS_PDP8I_MAJORSTATE_DEFER			
#define STATE_WORDCOUNT		REALCONS_PDP8I_MAJORSTATE_WORDCOUNT		
#define STATE_CURRENTADDRESS REALCONS_PDP8I_MAJORSTATE_CURRENTADDRESS
#define STATE_BREAK			REALCONS_PDP8I_MAJORSTATE_BREAK		

						   // Signals set by miscellaneous flip flops
// Ion indicates the 1 status of the INT.ENABLE flip - flop.
//		When lit, the interrupt control is enabled for
//		information exchange with an I / O device.
// Pause indicates the 1 status of the PAUSE flip - flop when lit.
//		The PAUSE flip - flop is set for 2.75 us by any IOT
//		instruction that requires generation of IOP pulses or by
//		any EAE instruction that require shifting of information.
// Run	indicates the 1 status of the RUN flip - flop. When lit,
//      the internal timing circuits are enabled and the machine
int32 realcons_flipflops; // see REALCONS_PDP8I_FLIFLOP_*

#define FLIPFLOP_SET(flipflop_bit) do { realcons_flipflops |= (flipflop_bit) ; } while(0)
#define FLIPFLOP_CLR(flipflop_bit) do { realcons_flipflops &= ~(flipflop_bit) ; } while(0)
#define FLIPFLOP_RUN	REALCONS_PDP8I_FLIPFLOP_RUN 
#define FLIPFLOP_PAUSE	REALCONS_PDP8I_FLIPFLOP_PAUSE
#define FLIPFLOP_ION	REALCONS_PDP8I_FLIPFLOP_ION

// Pointers to event handlers
// Events are called in SimH-code as pointers to functions in panel logic
//	console_controller_event_func_t	realcons_event_opcode_any ; // triggered after any opcode execution
//	console_controller_event_func_t realcons_event_opcode_halt ;

//  To be called as
// 		REALCONS_EVENT(cpu_realcons, realcons_event_opcode_halt) ;

/* Model proper use of physical MA and MB register,
 * they are update on every memory access
 * "MB" in sim_instr() seems just to be scratchpad and does not correspond to the real MB.
 *
 * "Usually, the contents of MA indicators denote the core memory address of the word currently or
 * previously read or written. After operation either the Dep or Exam key, the contents of MA
 * indicate the core memory address just examined or deposited into.
 * Usually, the contents of MB indicators designate the word just written at the core
 * memory address in MA."
 * TODO: MB does not contain read words also? Check later
 */
static uint16 pdp8_memread(uint32 memaddr) {
	realcons_memory_address_register = memaddr;
	return M[memaddr];
}
static uint16 pdp8_memset(uint32 memaddr, uint16 memval) {
	M[memaddr] = memval;
	realcons_memory_address_register = memaddr;
	realcons_memory_data_register = memval; // PDP8 doc: memory buffer = "MB"
	return memval;
}

#define PHYSICAL_MEMREAD(addr)	pdp8_memread(addr)
#define PHYSICAL_MEMWRITE(addr,val) pdp8_memset(addr,val)
#else
// disable extended simulation
#define FLIPFLOP_SET(flipflop_bit) do { } while(0)
#define MAJORSTATE_ENTER(state_bit) do { } while(0)
#define MAJORSTATE_CLEAR do { } while(0)
#define PHYSICAL_MEMREAD(addr)	M[addr]
#define PHYSICAL_MEMWRITE(addr,val) (M[addr]=(val))
#endif


t_stat sim_instr(void)
{
	int32 IR, MB, IF, DF, LAC, MQ;
	uint32 PC, MA;
	int32 device, pulse, temp, iot_data;
	t_stat reason;

	/* Restore register state */

	if (build_dev_tab())                                   /* build dev_tab */
		return SCPE_STOP;
	PC = saved_PC & 007777;                                 /* load local copies */
	IF = saved_PC & 070000;
	DF = saved_DF & 070000;
	LAC = saved_LAC & 017777;
	MQ = saved_MQ & 07777;
	int_req = INT_UPDATE;
	reason = 0;

	/* Main instruction fetch/decode loop */

	while (reason == 0) {                                   /* loop until halted */
		FLIPFLOP_SET(FLIPFLOP_RUN);
		FLIPFLOP_CLR(FLIPFLOP_PAUSE);
		if (int_req & INT_ION) //  ignore INT_CIF
			FLIPFLOP_SET(FLIPFLOP_ION);
		else
			FLIPFLOP_CLR(FLIPFLOP_ION);

		if (sim_interval <= 0) {                            /* check clock queue */
			if ((reason = sim_process_event()))
				break;
		}

		if (int_req > INT_PENDING) {                        /* interrupt? */
			int_req = int_req & ~INT_ION;                   /* interrupts off */
			SF = (UF << 6) | (IF >> 9) | (DF >> 12);        /* form save field */
			IF = IB = DF = UF = UB = 0;                     /* clear mem ext */
			PCQ_ENTRY;                                      /* save old PC */
			PHYSICAL_MEMWRITE(0, PC);                                      /* save PC in 0 */
			PC = 1;                                         /* fetch next from 1 */
		}

		MA = IF | PC;                                       /* form PC */
		if (sim_brk_summ &&
			sim_brk_test(MA, (1u << SIM_BKPT_V_SPC) | SWMASK('E'))) { /* breakpoint? */
			reason = STOP_IBKPT;                            /* stop simulation */
			break;
		}

		MAJORSTATE_CLEAR;
		MAJORSTATE_ENTER(STATE_FETCH);
		IR = PHYSICAL_MEMREAD(MA);                                         /* fetch instruction */
		if (sim_brk_summ &&
			sim_brk_test(IR, (2u << SIM_BKPT_V_SPC) | SWMASK('I'))) { /* breakpoint? */
			reason = STOP_OPBKPT;                            /* stop simulation */
			break;
		}
		PC = (PC + 1) & 07777;                              /* increment PC */
		int_req = int_req | INT_NO_ION_PENDING;             /* clear ION delay */
		sim_interval = sim_interval - 1;

		/* Instruction decoding.

		   The opcode (IR<0:2>), indirect flag (IR<3>), and page flag (IR<4>)
		   are decoded together.  This produces 32 decode points, four per
		   major opcode.  For IOT, the extra decode points are not useful;
		   for OPR, only the group flag (IR<3>) is used.

		   AND, TAD, ISZ, DCA calculate a full 15b effective address.
		   JMS, JMP calculate a 12b field-relative effective address.

		   Autoindex calculations always occur within the same field as the
		   instruction fetch.  The field must exist; otherwise, the instruction
		   fetched would be 0000, and indirect addressing could not occur.

		   Note that MA contains IF'PC.
		*/

		
		if (hst_lnt) {                                      /* history enabled? */
			int32 ea;

			hst_p = (hst_p + 1);                            /* next entry */
			if (hst_p >= hst_lnt)
				hst_p = 0;
			hst[hst_p].pc = MA | HIST_PC;                   /* save PC, IR, LAC, MQ */
			hst[hst_p].ir = IR;
			hst[hst_p].lac = LAC;
			hst[hst_p].mq = MQ;
			if (IR < 06000) {                               /* mem ref? */
				if (IR & 0200)
					ea = (MA & 077600) | (IR & 0177);
				else ea = IF | (IR & 0177);                 /* direct addr */
				if (IR & 0400) {                            /* indirect? */
					if (IR < 04000) {                       /* mem operand? */
						if ((ea & 07770) != 00010)
							ea = DF | PHYSICAL_MEMREAD(ea);
						else ea = DF | ((PHYSICAL_MEMREAD(ea) + 1) & 07777);
					}
					else {                                  /* no, jms/jmp */
						if ((ea & 07770) != 00010)
							ea = IB | PHYSICAL_MEMREAD(ea);
						else ea = IB | ((PHYSICAL_MEMREAD(ea) + 1) & 07777);
					}
				}
				hst[hst_p].ea = ea;                         /* save eff addr */
				hst[hst_p].opnd = M[ea];                    /* save operand */
			}
		}

#ifdef USE_REALCONS
		// 2nd instruction decoder, else too much "#ifdef USE_REALCONS" in following code
		{
			unsigned opcode = (IR >> 7) & 037;  /* decode IR<0:4> */
			if (opcode >= 000 && opcode <= 003)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_AND;
			else if (opcode >= 004 && opcode <= 007)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_TAD;
			else if (opcode >= 010 && opcode <= 013)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_ISZ;
			else if (opcode >= 014 && opcode <= 017)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_DCA;
			else if (opcode >= 020 && opcode <= 023)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_JMS;
			else if (opcode >= 024 && opcode <= 027)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_JMP;
			else if (opcode >= 030 && opcode <= 033)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_IOT;
			else if (opcode >= 034 && opcode <= 037)
				realcons_instruction_decode = REALCONS_PDP8I_INSTRUCTION_DECODE_OPR;
			else
				realcons_instruction_decode = 0;// never reached
		}
#endif

		switch ((IR >> 7) & 037) {                              /* decode IR<0:4> */

		/* Opcode 0, AND */

		case 000:                                           /* AND, dir, zero */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			LAC = LAC & (PHYSICAL_MEMREAD(MA) | 010000);
			break;

		case 001:                                           /* AND, dir, curr */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			LAC = LAC & (PHYSICAL_MEMREAD(MA) | 010000);
			break;

		case 002:                                           /* AND, indir, zero */
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
			LAC = LAC & (PHYSICAL_MEMREAD(MA) | 010000);
			break;

		case 003:                                           /* AND, indir, curr */
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (M[MA] + 1) & 07777);   /* incr before use */
			LAC = LAC & (PHYSICAL_MEMREAD(MA) | 010000);
			break;

			/* Opcode 1, TAD */

		case 004:                                           /* TAD, dir, zero */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			LAC = (LAC + PHYSICAL_MEMREAD(MA)) & 017777;
			break;

		case 005:                                           /* TAD, dir, curr */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			LAC = (LAC + PHYSICAL_MEMREAD(MA)) & 017777;
			break;

		case 006:                                           /* TAD, indir, zero */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			LAC = (LAC + PHYSICAL_MEMREAD(MA)) & 017777;
			break;

		case 007:                                           /* TAD, indir, curr */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			LAC = (LAC + PHYSICAL_MEMREAD(MA)) & 017777;
			break;

			/* Opcode 2, ISZ */

		case 010:                                           /* ISZ, dir, zero */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			PHYSICAL_MEMWRITE(MA, MB = (PHYSICAL_MEMREAD(MA) + 1) & 07777);               /* field must exist */
			if (MB == 0)
				PC = (PC + 1) & 07777;
			break;

		case 011:                                           /* ISZ, dir, curr */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			PHYSICAL_MEMWRITE(MA, MB = (PHYSICAL_MEMREAD(MA) + 1) & 07777);               /* field must exist */
			//M[MA] = MB = (M[MA] + 1) & 07777;               /* field must exist */
			if (MB == 0)
				PC = (PC + 1) & 07777;
			break;

		case 012:                                           /* ISZ, indir, zero */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			MB = (PHYSICAL_MEMREAD(MA) + 1) & 07777;
			if (MEM_ADDR_OK(MA))
				PHYSICAL_MEMWRITE(MA, MB);
			if (MB == 0)
				PC = (PC + 1) & 07777;
			break;

		case 013:                                           /* ISZ, indir, curr */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			MB = (PHYSICAL_MEMREAD(MA) + 1) & 07777;
			if (MEM_ADDR_OK(MA))
				PHYSICAL_MEMWRITE(MA, MB);
			if (MB == 0)
				PC = (PC + 1) & 07777;
			break;

			/* Opcode 3, DCA */

		case 014:                                           /* DCA, dir, zero */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			PHYSICAL_MEMWRITE(MA, LAC & 07777);
			LAC = LAC & 010000;
			break;

		case 015:                                           /* DCA, dir, curr */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			PHYSICAL_MEMWRITE(MA, LAC & 07777);
			LAC = LAC & 010000;
			break;

		case 016:                                           /* DCA, indir, zero */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			if (MEM_ADDR_OK(MA))
				PHYSICAL_MEMWRITE(MA, LAC & 07777);
			LAC = LAC & 010000;
			break;

		case 017:                                           /* DCA, indir, curr */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = DF | PHYSICAL_MEMREAD(MA);
			else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);   /* incr before use */
			if (MEM_ADDR_OK(MA))
				PHYSICAL_MEMWRITE(MA, LAC & 07777);
			LAC = LAC & 010000;
			break;

			/* Opcode 4, JMS.  From Bernhard Baehr's description of the TSC8-75:

			   (In user mode) the current JMS opcode is moved to the ERIOT register, the ECDF
			   flag is cleared. The address of the JMS instruction is loaded into the ERTB
			   register and the TSC8-75 I/O flag is raised. When the TSC8-75 is enabled, the
			   target addess of the JMS is loaded into PC, but nothing else (loading of IF, UF,
			   clearing the interrupt inhibit flag, storing of the return address in the first
			   word of the subroutine) happens. When the TSC8-75 is disabled, the JMS is performed
			   as usual. */

		case 020:                                           /* JMS, dir, zero */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			PCQ_ENTRY;
			MA = IR & 0177;                                 /* dir addr, page zero */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
			}
			if (UF && tsc_enb) {                            /* user mode, TSC enab? */
				tsc_pc = (PC - 1) & 07777;                  /* save PC */
				int_req = int_req | INT_TSC;                /* request intr */
			}
			else {                                          /* normal */
				IF = IB;                                    /* change IF */
				UF = UB;                                    /* change UF */
				int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
				MA = IF | MA;
				if (MEM_ADDR_OK(MA))
					PHYSICAL_MEMWRITE(MA, PC);
			}
			PC = (MA + 1) & 07777;
			break;

		case 021:                                           /* JMS, dir, curr */
			MAJORSTATE_ENTER(STATE_EXECUTE);
			PCQ_ENTRY;
			MA = (MA & 007600) | (IR & 0177);               /* dir addr, curr page */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
			}
			if (UF && tsc_enb) {                            /* user mode, TSC enab? */
				tsc_pc = (PC - 1) & 07777;                  /* save PC */
				int_req = int_req | INT_TSC;                /* request intr */
			}
			else {                                          /* normal */
				IF = IB;                                    /* change IF */
				UF = UB;                                    /* change UF */
				int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
				MA = IF | MA;
				if (MEM_ADDR_OK(MA))
					PHYSICAL_MEMWRITE(MA, PC);
			}
			PC = (MA + 1) & 07777;
			break;

		case 022:                                           /* JMS, indir, zero */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			PCQ_ENTRY;
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = PHYSICAL_MEMREAD(MA);
			else MA = PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			//else MA = (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
			}
			if (UF && tsc_enb) {                            /* user mode, TSC enab? */
				tsc_pc = (PC - 1) & 07777;                  /* save PC */
				int_req = int_req | INT_TSC;                /* request intr */
			}
			else {                                          /* normal */
				IF = IB;                                    /* change IF */
				UF = UB;                                    /* change UF */
				int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
				MA = IF | MA;
				if (MEM_ADDR_OK(MA))
					PHYSICAL_MEMWRITE(MA, PC);
			}
			PC = (MA + 1) & 07777;
			break;

		case 023:                                           /* JMS, indir, curr */
			MAJORSTATE_ENTER(STATE_DEFER);
			MAJORSTATE_ENTER(STATE_EXECUTE);
			PCQ_ENTRY;
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = PHYSICAL_MEMREAD(MA);
			else MA = PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			//else MA = (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
			}
			if (UF && tsc_enb) {                            /* user mode, TSC enab? */
				tsc_pc = (PC - 1) & 07777;                  /* save PC */
				int_req = int_req | INT_TSC;                /* request intr */
			}
			else {                                          /* normal */
				IF = IB;                                    /* change IF */
				UF = UB;                                    /* change UF */
				int_req = int_req | INT_NO_CIF_PENDING;     /* clr intr inhibit */
				MA = IF | MA;
				if (MEM_ADDR_OK(MA))
					PHYSICAL_MEMWRITE(MA, PC);
			}
			PC = (MA + 1) & 07777;
			break;

			/* Opcode 5, JMP.  From Bernhard Baehr's description of the TSC8-75:

			   (In user mode) the current JMP opcode is moved to the ERIOT register, the ECDF
			   flag is cleared. The address of the JMP instruction is loaded into the ERTB
			   register and the TSC8-75 I/O flag is raised. Then the JMP is performed as usual
			   (including the setting of IF, UF and clearing the interrupt inhibit flag). */


		case 024:                                           /* JMP, dir, zero */
			//MAJORSTATE(STATE_EXECUTE) ; 				   // JMP without EXECUTE?
			PCQ_ENTRY;
			MA = IR & 0177;                                 /* dir addr, page zero */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
				if (tsc_enb) {                              /* TSC8 enabled? */
					tsc_pc = (PC - 1) & 07777;              /* save PC */
					int_req = int_req | INT_TSC;            /* request intr */
				}
			}
			IF = IB;                                        /* change IF */
			UF = UB;                                        /* change UF */
			int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
			PC = MA;
			break;

			/* If JMP direct, also check for idle (KSF/JMP *-1) and infinite loop */

		case 025:                                           /* JMP, dir, curr */
			//MAJORSTATE(STATE_EXECUTE) ; 				   // JMP without EXECUTE?
			PCQ_ENTRY;
			MA = (MA & 007600) | (IR & 0177);               /* dir addr, curr page */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
				if (tsc_enb) {                              /* TSC8 enabled? */
					tsc_pc = (PC - 1) & 07777;              /* save PC */
					int_req = int_req | INT_TSC;            /* request intr */
				}
			}
			if (sim_idle_enab &&                            /* idling enabled? */
				(IF == IB)) {                               /* to same bank? */
				if (MA == ((PC - 2) & 07777)) {             /* 1) JMP *-1? */
					if (!(int_req & (INT_ION | INT_TTI)) &&   /*    iof, TTI flag off? */
						(PHYSICAL_MEMREAD(IB | ((PC - 2) & 07777)) == OP_KSF)) /*  next is KSF? */
						sim_idle(TMR_CLK, FALSE);          /* we're idle */
				}                                       /* end JMP *-1 */
				else if (MA == ((PC - 1) & 07777)) {        /* 2) JMP *? */
					if (!(int_req & INT_ION))               /*    iof? */
						reason = STOP_LOOP;                 /* then infinite loop */
					else if (!(int_req & INT_ALL))          /*    ion, not intr? */
						sim_idle(TMR_CLK, FALSE);          /* we're idle */
				}                                       /* end JMP */
			}                                           /* end idle enabled */
			IF = IB;                                        /* change IF */
			UF = UB;                                        /* change UF */
			int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
			PC = MA;
			break;

		case 026:                                           /* JMP, indir, zero */
			//MAJORSTATE(STATE_EXECUTE) ; 				   // JMP without EXECUTE?
			MAJORSTATE_ENTER(STATE_DEFER);
			PCQ_ENTRY;
			MA = IF | (IR & 0177);                          /* dir addr, page zero */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = PHYSICAL_MEMREAD(MA);
			else MA = PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			//else MA = (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
				if (tsc_enb) {                              /* TSC8 enabled? */
					tsc_pc = (PC - 1) & 07777;              /* save PC */
					int_req = int_req | INT_TSC;            /* request intr */
				}
			}
			IF = IB;                                        /* change IF */
			UF = UB;                                        /* change UF */
			int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
			PC = MA;
			break;

		case 027:                                           /* JMP, indir, curr */
			//MAJORSTATE(STATE_EXECUTE) ; 				   // JMP without EXECUTE?
			MAJORSTATE_ENTER(STATE_DEFER); 					    // JMP without EXECUTE?
			PCQ_ENTRY;
			MA = (MA & 077600) | (IR & 0177);               /* dir addr, curr page */
			if ((MA & 07770) != 00010)                      /* indirect; autoinc? */
				MA = PHYSICAL_MEMREAD(MA);
			else MA = PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			//else MA = (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777);        /* incr before use */
			if (UF) {                                       /* user mode? */
				tsc_ir = IR;                                /* save instruction */
				tsc_cdf = 0;                                /* clear flag */
				if (tsc_enb) {                              /* TSC8 enabled? */
					tsc_pc = (PC - 1) & 07777;              /* save PC */
					int_req = int_req | INT_TSC;            /* request intr */
				}
			}
			IF = IB;                                        /* change IF */
			UF = UB;                                        /* change UF */
			int_req = int_req | INT_NO_CIF_PENDING;         /* clr intr inhibit */
			PC = MA;
			break;

			/* Opcode 7, OPR group 1 */
			// all without STATE_EXECUTE

		case 034:case 035:                                  /* OPR, group 1 */
			switch ((IR >> 4) & 017) {                      /* decode IR<4:7> */
			case 0:                                         /* nop */
				break;
			case 1:                                         /* CML */
				LAC = LAC ^ 010000;
				break;
			case 2:                                         /* CMA */
				LAC = LAC ^ 07777;
				break;
			case 3:                                         /* CMA CML */
				LAC = LAC ^ 017777;
				break;
			case 4:                                         /* CLL */
				LAC = LAC & 07777;
				break;
			case 5:                                         /* CLL CML = STL */
				LAC = LAC | 010000;
				break;
			case 6:                                         /* CLL CMA */
				LAC = (LAC ^ 07777) & 07777;
				break;
			case 7:                                         /* CLL CMA CML */
				LAC = (LAC ^ 07777) | 010000;
				break;
			case 010:                                       /* CLA */
				LAC = LAC & 010000;
				break;
			case 011:                                       /* CLA CML */
				LAC = (LAC & 010000) ^ 010000;
				break;
			case 012:                                       /* CLA CMA = STA */
				LAC = LAC | 07777;
				break;
			case 013:                                       /* CLA CMA CML */
				LAC = (LAC | 07777) ^ 010000;
				break;
			case 014:                                       /* CLA CLL */
				LAC = 0;
				break;
			case 015:                                       /* CLA CLL CML */
				LAC = 010000;
				break;
			case 016:                                       /* CLA CLL CMA */
				LAC = 07777;
				break;
			case 017:                                       /* CLA CLL CMA CML */
				LAC = 017777;
				break;
			}                                           /* end switch opers */

			if (IR & 01)                                    /* IAC */
				LAC = (LAC + 1) & 017777;
			switch ((IR >> 1) & 07) {                       /* decode IR<8:10> */
			case 0:                                         /* nop */
				break;
			case 1:                                         /* BSW */
				LAC = (LAC & 010000) | ((LAC >> 6) & 077) | ((LAC & 077) << 6);
				break;
			case 2:                                         /* RAL */
				LAC = ((LAC << 1) | (LAC >> 12)) & 017777;
				break;
			case 3:                                         /* RTL */
				LAC = ((LAC << 2) | (LAC >> 11)) & 017777;
				break;
			case 4:                                         /* RAR */
				LAC = ((LAC >> 1) | (LAC << 12)) & 017777;
				break;
			case 5:                                         /* RTR */
				LAC = ((LAC >> 2) | (LAC << 11)) & 017777;
				break;
			case 6:                                         /* RAL RAR - undef */
				LAC = LAC & (IR | 010000);                  /* uses AND path */
				break;
			case 7:                                         /* RTL RTR - undef */
				LAC = (LAC & 010000) | (MA & 07600) | (IR & 0177);
				break;                                      /* uses address path */
			}                                           /* end switch shifts */
			break;                                          /* end group 1 */

	/* OPR group 2.  From Bernhard Baehr's description of the TSC8-75:

	   (In user mode) HLT (7402), OSR (7404) and microprogrammed combinations with
	   HLT and OSR: Additional to raising a user mode interrupt, the current OPR
	   opcode is moved to the ERIOT register and the ECDF flag is cleared. */

		case 036:case 037:                                  /* OPR, groups 2, 3 */
			if ((IR & 01) == 0) {                           /* group 2 */
				switch ((IR >> 3) & 017) {                  /* decode IR<6:8> */
				case 0:                                     /* nop */
					break;
				case 1:                                     /* SKP */
					PC = (PC + 1) & 07777;
					break;
				case 2:                                     /* SNL */
					if (LAC >= 010000)
						PC = (PC + 1) & 07777;
					break;
				case 3:                                     /* SZL */
					if (LAC < 010000)
						PC = (PC + 1) & 07777;
					break;
				case 4:                                     /* SZA */
					if ((LAC & 07777) == 0)
						PC = (PC + 1) & 07777;
					break;
				case 5:                                     /* SNA */
					if ((LAC & 07777)
						!= 0) PC = (PC + 1) & 07777;
					break;
				case 6:                                     /* SZA | SNL */
					if ((LAC == 0) || (LAC >= 010000))
						PC = (PC + 1) & 07777;
					break;
				case 7:                                     /* SNA & SZL */
					if ((LAC != 0) && (LAC < 010000))
						PC = (PC + 1) & 07777;
					break;
				case 010:                                   /* SMA */
					if ((LAC & 04000) != 0)
						PC = (PC + 1) & 07777;
					break;
				case 011:                                   /* SPA */
					if ((LAC & 04000) == 0)
						PC = (PC + 1) & 07777;
					break;
				case 012:                                   /* SMA | SNL */
					if (LAC >= 04000)
						PC = (PC + 1) & 07777;
					break;
				case 013:                                   /* SPA & SZL */
					if (LAC < 04000)
						PC = (PC + 1) & 07777;
					break;
				case 014:                                   /* SMA | SZA */
					if (((LAC & 04000) != 0) || ((LAC & 07777) == 0))
						PC = (PC + 1) & 07777;
					break;
				case 015:                                   /* SPA & SNA */
					if (((LAC & 04000) == 0) && ((LAC & 07777) != 0))
						PC = (PC + 1) & 07777;
					break;
				case 016:                                   /* SMA | SZA | SNL */
					if ((LAC >= 04000) || (LAC == 0))
						PC = (PC + 1) & 07777;
					break;
				case 017:                                   /* SPA & SNA & SZL */
					if ((LAC < 04000) && (LAC != 0))
						PC = (PC + 1) & 07777;
					break;
				}                                       /* end switch skips */
				if (IR & 0200)                              /* CLA */
					LAC = LAC & 010000;
				if ((IR & 06) && UF) {                      /* user mode? */
					int_req = int_req | INT_UF;             /* request intr */
					tsc_ir = IR;                            /* save instruction */
					tsc_cdf = 0;                            /* clear flag */
				}
				else {
					if (IR & 04)                            /* OSR */
						LAC = LAC | OSR;
					if (IR & 02)                            /* HLT */
						reason = STOP_HALT;
				}
				break;
			}                                           /* end if group 2 */

/* OPR group 3 standard

   MQA!MQL exchanges AC and MQ, as follows:

		temp = MQ;
		MQ = LAC & 07777;
		LAC = LAC & 010000 | temp;
*/

			temp = MQ;                                      /* group 3 */
			if (IR & 0200)                                  /* CLA */
				LAC = LAC & 010000;
			if (IR & 0020) {                                /* MQL */
				MQ = LAC & 07777;
				LAC = LAC & 010000;
			}
			if (IR & 0100)                                  /* MQA */
				LAC = LAC | temp;
			if ((IR & 0056) && (cpu_unit.flags & UNIT_NOEAE)) {
				reason = stop_inst;                         /* EAE not present */
				break;
			}

			/* OPR group 3 EAE

			   The EAE operates in two modes:

					Mode A, PDP-8/I compatible
					Mode B, extended capability

			   Mode B provides eight additional subfunctions; in addition, some
			   of the Mode A functions operate differently in Mode B.

			   The mode switch instructions are decoded explicitly and cannot be
			   microprogrammed with other EAE functions (SWAB performs an MQL as
			   part of standard group 3 decoding).  If mode switching is decoded,
			   all other EAE timing is suppressed.
			*/

			if (IR == 07431) {                              /* SWAB */
				emode = 1;                                  /* set mode flag */
				break;
			}
			if (IR == 07447) {                              /* SWBA */
				emode = gtf = 0;                            /* clear mode, gtf */
				break;
			}

			/* If not switching modes, the EAE operation is determined by the mode
			   and IR<6,8:10>:

			   <6:10>       mode A          mode B          comments

			   0x000        NOP             NOP
			   0x001        SCL             ACS
			   0x010        MUY             MUY             if mode B, next = address
			   0x011        DVI             DVI             if mode B, next = address
			   0x100        NMI             NMI             if mode B, clear AC if
															 result = 4000'0000
			   0x101        SHL             SHL             if mode A, extra shift
			   0x110        ASR             ASR             if mode A, extra shift
			   0x111        LSR             LSR             if mode A, extra shift
			   1x000        SCA             SCA
			   1x001        SCA + SCL       DAD
			   1x010        SCA + MUY       DST
			   1x011        SCA + DVI       SWBA            NOP if not detected earlier
			   1x100        SCA + NMI       DPSZ
			   1x101        SCA + SHL       DPIC            must be combined with MQA!MQL
			   1x110        SCA + ASR       DCM             must be combined with MQA!MQL
			   1x111        SCA + LSR       SAM

			   EAE instructions which fetch memory operands use the CPU's DEFER
			   state to read the first word; if the address operand is in locations
			   x0010 - x0017, it is autoincremented.
			*/

			if (emode == 0)                                 /* mode A? clr gtf */
				gtf = 0;
			switch ((IR >> 1) & 027) {                      /* decode IR<6,8:10> */

			case 020:                                       /* mode A, B: SCA */
				LAC = LAC | SC;
				break;
			case 000:                                       /* mode A, B: NOP */
				break;

			case 021:                                       /* mode B: DAD */
				if (emode) {
					MA = IF | PC;
					if ((MA & 07770) != 00010)              /* indirect; autoinc? */
						MA = DF | PHYSICAL_MEMREAD(MA);
					else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
					//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
					MQ = MQ + PHYSICAL_MEMREAD(MA);
					MA = DF | ((MA + 1) & 07777);
					LAC = (LAC & 07777) + PHYSICAL_MEMREAD(MA) + (MQ >> 12);
					MQ = MQ & 07777;
					PC = (PC + 1) & 07777;
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 001:                                       /* mode B: ACS */
				if (emode) {
					SC = LAC & 037;
					LAC = LAC & 010000;
				}
				else {                                      /* mode A: SCL */
					SC = (~PHYSICAL_MEMREAD(IF | PC)) & 037;
					PC = (PC + 1) & 07777;
				}
				break;

			case 022:                                       /* mode B: DST */
				if (emode) {
					MA = IF | PC;
					if ((MA & 07770) != 00010)              /* indirect; autoinc? */
						MA = DF | PHYSICAL_MEMREAD(MA);
					else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
					//else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
					if (MEM_ADDR_OK(MA))
						PHYSICAL_MEMWRITE(MA, MQ & 07777);
					MA = DF | ((MA + 1) & 07777);
					if (MEM_ADDR_OK(MA))
						PHYSICAL_MEMWRITE(MA, LAC & 07777);
					PC = (PC + 1) & 07777;
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 002:                                       /* MUY */
				MA = IF | PC;
				if (emode) {                                /* mode B: defer */
					if ((MA & 07770) != 00010)              /* indirect; autoinc? */
						MA = DF | PHYSICAL_MEMREAD(MA);
					else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
					//else MA = DF | (M[MA] = (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
				}
				temp = (MQ * PHYSICAL_MEMREAD(MA)) + (LAC & 07777);
				LAC = (temp >> 12) & 07777;
				MQ = temp & 07777;
				PC = (PC + 1) & 07777;
				SC = 014;                                   /* 12 shifts */
				break;

			case 023:                                       /* mode B: SWBA */
				if (emode)
					break;
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 003:                                       /* DVI */
				MA = IF | PC;
				if (emode) {                                /* mode B: defer */
					if ((MA & 07770) != 00010)              /* indirect; autoinc? */
						MA = DF | PHYSICAL_MEMREAD(MA);
					else MA = DF | PHYSICAL_MEMWRITE(MA, (PHYSICAL_MEMREAD(MA) + 1) & 07777); /* incr before use */
					//else MA = DF | (M[MA] = (M[MA] + 1) & 07777); /* incr before use */
				}
				if ((LAC & 07777) >= PHYSICAL_MEMREAD(MA)) {               /* overflow? */
					LAC = LAC | 010000;                     /* set link */
					MQ = ((MQ << 1) + 1) & 07777;           /* rotate MQ */
					SC = 0;                                 /* no shifts */
				}
				else {
					temp = ((LAC & 07777) << 12) | MQ;
					MQ = temp / PHYSICAL_MEMREAD(MA);
					LAC = temp % PHYSICAL_MEMREAD(MA);
					SC = 015;                               /* 13 shifts */
				}
				PC = (PC + 1) & 07777;
				break;

			case 024:                                       /* mode B: DPSZ */
				if (emode) {
					if (((LAC | MQ) & 07777) == 0)
						PC = (PC + 1) & 07777;
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 004:                                       /* NMI */
				temp = (LAC << 12) | MQ;                    /* preserve link */
				for (SC = 0; ((temp & 017777777) != 0) &&
					(temp & 040000000) == ((temp << 1) & 040000000); SC++)
					temp = temp << 1;
				LAC = (temp >> 12) & 017777;
				MQ = temp & 07777;
				if (emode && ((LAC & 07777) == 04000) && (MQ == 0))
					LAC = LAC & 010000;                     /* clr if 4000'0000 */
				break;

			case 025:                                       /* mode B: DPIC */
				if (emode) {
					temp = (LAC + 1) & 07777;               /* SWP already done! */
					LAC = MQ + (temp == 0);
					MQ = temp;
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 5:                                         /* SHL */
				SC = (PHYSICAL_MEMREAD(IF | PC) & 037) + (emode ^ 1);      /* shift+1 if mode A */
				//SC = (M[IF | PC] & 037) + (emode ^ 1);      /* shift+1 if mode A */
				if (SC > 25)                                /* >25? result = 0 */
					temp = 0;
				else temp = ((LAC << 12) | MQ) << SC;       /* <=25? shift LAC:MQ */
				LAC = (temp >> 12) & 017777;
				MQ = temp & 07777;
				PC = (PC + 1) & 07777;
				SC = emode ? 037 : 0;                         /* SC = 0 if mode A */
				break;

			case 026:                                       /* mode B: DCM */
				if (emode) {
					temp = (-LAC) & 07777;                  /* SWP already done! */
					LAC = (MQ ^ 07777) + (temp == 0);
					MQ = temp;
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 6:                                         /* ASR */
				SC = (PHYSICAL_MEMREAD(IF | PC) & 037) + (emode ^ 1);      /* shift+1 if mode A */
				temp = ((LAC & 07777) << 12) | MQ;          /* sext from AC0 */
				if (LAC & 04000)
					temp = temp | ~037777777;
				if (emode && (SC != 0))
					gtf = (temp >> (SC - 1)) & 1;
				if (SC > 25)
					temp = (LAC & 04000) ? -1 : 0;
				else temp = temp >> SC;
				LAC = (temp >> 12) & 017777;
				MQ = temp & 07777;
				PC = (PC + 1) & 07777;
				SC = emode ? 037 : 0;                         /* SC = 0 if mode A */
				break;

			case 027:                                       /* mode B: SAM */
				if (emode) {
					temp = LAC & 07777;
					LAC = MQ + (temp ^ 07777) + 1;          /* L'AC = MQ - AC */
					gtf = (temp <= MQ) ^ ((temp ^ MQ) >> 11);
					break;
				}
				LAC = LAC | SC;                             /* mode A: SCA then */
			case 7:                                         /* LSR */
				SC = (PHYSICAL_MEMREAD(IF | PC) & 037) + (emode ^ 1);      /* shift+1 if mode A */
				temp = ((LAC & 07777) << 12) | MQ;          /* clear link */
				if (emode && (SC != 0))
					gtf = (temp >> (SC - 1)) & 1;
				if (SC > 24)                                /* >24? result = 0 */
					temp = 0;
				else temp = temp >> SC;                     /* <=24? shift AC:MQ */
				LAC = (temp >> 12) & 07777;
				MQ = temp & 07777;
				PC = (PC + 1) & 07777;
				SC = emode ? 037 : 0;                         /* SC = 0 if mode A */
				break;
			}                                           /* end switch */
			break;                                          /* end case 7 */

	/* Opcode 6, IOT.  From Bernhard Baehr's description of the TSC8-75:

	   (In user mode) Additional to raising a user mode interrupt, the current IOT
	   opcode is moved to the ERIOT register. When the IOT is a CDF instruction (62x1),
	   the ECDF flag is set, otherwise it is cleared. */

		case 030:case 031:case 032:case 033:                /* IOT */
			if (UF) {                                       /* privileged? */
				int_req = int_req | INT_UF;                 /* request intr */
				tsc_ir = IR;                                /* save instruction */
				if ((IR & 07707) == 06201)                  /* set/clear flag */
					tsc_cdf = 1;
				else tsc_cdf = 0;
				break;
			}
			device = (IR >> 3) & 077;                       /* device = IR<3:8> */

	// Oscar Vermeulen: the IOT ION, IOF do not light pause, anything else does:
			if (device > 02)
				FLIPFLOP_SET(FLIPFLOP_PAUSE);

			pulse = IR & 07;                                /* pulse = IR<9:11> */
			iot_data = LAC & 07777;                         /* AC unchanged */
			switch (device) {                               /* decode IR<3:8> */

			case 000:                                       /* CPU control */
				switch (pulse) {                            /* decode IR<9:11> */

				case 0:                                     /* SKON */
					if (int_req & INT_ION)
						PC = (PC + 1) & 07777;
					int_req = int_req & ~INT_ION;
					break;

				case 1:                                     /* ION */
					int_req = (int_req | INT_ION) & ~INT_NO_ION_PENDING;
					break;

				case 2:                                     /* IOF */
					int_req = int_req & ~INT_ION;
					break;

				case 3:                                     /* SRQ */
					if (int_req & INT_ALL)
						PC = (PC + 1) & 07777;
					break;

				case 4:                                     /* GTF */
					LAC = (LAC & 010000) |
						((LAC & 010000) >> 1) | (gtf << 10) |
						(((int_req & INT_ALL) != 0) << 9) |
						(((int_req & INT_ION) != 0) << 7) | SF;
					break;

				case 5:                                     /* RTF */
					gtf = ((LAC & 02000) >> 10);
					UB = (LAC & 0100) >> 6;
					IB = (LAC & 0070) << 9;
					DF = (LAC & 0007) << 12;
					LAC = ((LAC & 04000) << 1) | iot_data;
					int_req = (int_req | INT_ION) & ~INT_NO_CIF_PENDING;
					break;

				case 6:                                     /* SGT */
					if (gtf)
						PC = (PC + 1) & 07777;
					break;

				case 7:                                     /* CAF */
					gtf = 0;
					emode = 0;
					int_req = int_req & INT_NO_CIF_PENDING;
					dev_done = 0;
					int_enable = INT_INIT_ENABLE;
					LAC = 0;
					reset_all(1);                          /* reset all dev */
					break;
				}                                       /* end switch pulse */
				break;                                      /* end case 0 */

			case 020:case 021:case 022:case 023:
			case 024:case 025:case 026:case 027:            /* memory extension */

		 // Oscar Vermeulen:  Memory extension does not trigger IOP pauses --> do not light pause

				switch (pulse) {                            /* decode IR<9:11> */

				case 1:                                     /* CDF */
					DF = (IR & 0070) << 9;
					break;

				case 2:                                     /* CIF */
					IB = (IR & 0070) << 9;
					int_req = int_req & ~INT_NO_CIF_PENDING;
					break;

				case 3:                                     /* CDF CIF */
					DF = IB = (IR & 0070) << 9;
					int_req = int_req & ~INT_NO_CIF_PENDING;
					break;

				case 4:
					switch (device & 07) {                  /* decode IR<6:8> */

					case 0:                                 /* CINT */
						int_req = int_req & ~INT_UF;
						break;

					case 1:                                 /* RDF */
						LAC = LAC | (DF >> 9);
						break;

					case 2:                                 /* RIF */
						LAC = LAC | (IF >> 9);
						break;

					case 3:                                 /* RIB */
						LAC = LAC | SF;
						break;

					case 4:                                 /* RMF */
						UB = (SF & 0100) >> 6;
						IB = (SF & 0070) << 9;
						DF = (SF & 0007) << 12;
						int_req = int_req & ~INT_NO_CIF_PENDING;
						break;

					case 5:                                 /* SINT */
						if (int_req & INT_UF)
							PC = (PC + 1) & 07777;
						break;

					case 6:                                 /* CUF */
						UB = 0;
						int_req = int_req & ~INT_NO_CIF_PENDING;
						break;

					case 7:                                 /* SUF */
						UB = 1;
						int_req = int_req & ~INT_NO_CIF_PENDING;
						break;
					}                                   /* end switch device */
					break;

				default:
					reason = stop_inst;
					break;
				}                                       /* end switch pulse */
				break;                                      /* end case 20-27 */

			case 010:                                       /* power fail */
				switch (pulse) {                            /* decode IR<9:11> */

				case 1:                                     /* SBE */
					break;

				case 2:                                     /* SPL */
					if (int_req & INT_PWR)
						PC = (PC + 1) & 07777;
					break;

				case 3:                                     /* CAL */
					int_req = int_req & ~INT_PWR;
					break;

				default:
					reason = stop_inst;
					break;
				}                                       /* end switch pulse */
				break;                                      /* end case 10 */

			default:                                        /* I/O device */
				if (dev_tab[device]) {                      /* dev present? */

					// Oscar Vermeulen: Any other device will trigger IOP --> light pause:
					FLIPFLOP_SET(FLIPFLOP_PAUSE);

					iot_data = dev_tab[device](IR, iot_data);
					LAC = (LAC & 010000) | (iot_data & 07777);
					if (iot_data & IOT_SKP)
						PC = (PC + 1) & 07777;
					else {
						// Oscar Vermeulen:  simh does not distinguish between the various Data Break types. 
						// WC, CA and Break are lit up jointly on the PiDP. Although this can be improved upon.
						MAJORSTATE_ENTER(STATE_WORDCOUNT);
						MAJORSTATE_ENTER(STATE_CURRENTADDRESS);
						MAJORSTATE_ENTER(STATE_BREAK);
					}
					if (iot_data >= IOT_REASON)
						reason = iot_data >> IOT_V_REASON;
				}
				else reason = stop_inst;                    /* stop on flag */
				break;
			}                                           /* end switch device */

			// device I/O ready. Do not clear PAUSE flipflop state, needs to be displayed by console
			//FLIPFLOP_CLR(FLIPFLOP_PAUSE);	
			// Oscar Vermeulen: clear pause led, clear WC led, clear CA led, clear Break led
			break;                                          /* end case IOT */
		}                                               /* end switch opcode */


#ifdef USE_REALCONS
	// update state, used by for realcons. Code copy from after while()
		saved_PC = IF | (PC & 07777);                           /* save copies */
		saved_DF = DF & 070000;
		saved_LAC = LAC & 017777;
		saved_MQ = MQ & 07777;

														// show last major state
		realcons_service(cpu_realcons, 1); // high speed call

		// check if the ENABLE/HALT switch was set to HALT
		if (cpu_realcons->connected && realcons_console_halt) {
			reason = SCPE_STOP; // transition is triggered at end of instr loop
			// last major state remains
		}
#endif
	}                                                   /* end while */


	/* Simulation halted */
	FLIPFLOP_CLR(FLIPFLOP_RUN);

	saved_PC = IF | (PC & 07777);                           /* save copies */
	saved_DF = DF & 070000;
	saved_LAC = LAC & 017777;
	saved_MQ = MQ & 07777;
	pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
	return reason;
}                                                       /* end sim_instr */

/* Reset routine */

t_stat cpu_reset(DEVICE *dptr)
{
	int_req = (int_req & ~INT_ION) | INT_NO_CIF_PENDING;
	saved_DF = IB = saved_PC & 070000;
	UF = UB = gtf = emode = 0;
	pcq_r = find_reg("PCQ", NULL, dptr);
	if (pcq_r)
		pcq_r->qptr = 0;
	else return SCPE_IERR;
	MAJORSTATE_CLEAR;
#ifdef USE_REALCONS
	// initialize realcons cpu state extension here
	realcons_instruction_decode = 0;
	realcons_flipflops = 0;
#endif
	sim_brk_types = SWMASK('E') | SWMASK('I');
	sim_brk_dflt = SWMASK('E');
	return SCPE_OK;
}

/* Set PC for boot (PC<14:12> will typically be 0) */

void cpu_set_bootpc(int32 pc)
{
	saved_PC = pc;                                          /* set PC, IF */
	saved_DF = IB = pc & 070000;                            /* set IB, DF */
	return;
}

/* Memory examine */

t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	if (vptr != NULL)
		*vptr = PHYSICAL_MEMREAD(addr) & 07777;
	return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	PHYSICAL_MEMWRITE(addr, val & 07777);
	return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	int32 mc = 0;
	uint32 i;

	if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
		return SCPE_ARG;
	for (i = val; i < MEMSIZE; i++)
		mc = mc | M[i];
	if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
		return SCPE_OK;
	MEMSIZE = val;
	for (i = MEMSIZE; i < MAXMEMSIZE; i++)
		M[i] = 0;
	return SCPE_OK;
}

/* Change device number for a device */

t_stat set_dev (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	DEVICE *dptr;
	DIB *dibp;
	uint32 newdev;
	t_stat r;

	if (cptr == NULL)
		return SCPE_ARG;
	if (uptr == NULL)
		return SCPE_IERR;
	dptr = find_dev_from_unit(uptr);
	if (dptr == NULL)
		return SCPE_IERR;
	dibp = (DIB *)dptr->ctxt;
	if (dibp == NULL)
		return SCPE_IERR;
	newdev = get_uint(cptr, 8, DEV_MAX - 1, &r);           /* get new */
	if ((r != SCPE_OK) || (newdev == dibp->dev))
		return r;
	dibp->dev = newdev;                                     /* store */
	return SCPE_OK;
}

/* Show device number for a device */

t_stat show_dev (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
	DEVICE *dptr;
	DIB *dibp;

	if (uptr == NULL)
		return SCPE_IERR;
	dptr = find_dev_from_unit(uptr);
	if (dptr == NULL)
		return SCPE_IERR;
	dibp = (DIB *)dptr->ctxt;
	if (dibp == NULL)
		return SCPE_IERR;
	fprintf(st, "devno=%02o", dibp->dev);
	if (dibp->num > 1)
		fprintf(st, "-%2o", dibp->dev + dibp->num - 1);
	return SCPE_OK;
}

/* CPU device handler - should never get here! */

int32 bad_dev(int32 IR, int32 AC)
{
	return (SCPE_IERR << IOT_V_REASON) | AC;                /* broken! */
}

/* Build device dispatch table */

t_bool build_dev_tab(void)
{
	DEVICE *dptr;
	DIB *dibp;
	uint32 i, j;
	static const uint8 std_dev[] = {
		000, 010, 020, 021, 022, 023, 024, 025, 026, 027
	};

	for (i = 0; i < DEV_MAX; i++)                           /* clr table */
		dev_tab[i] = NULL;
	for (i = 0; i < ((uint32) sizeof(std_dev)); i++)       /* std entries */
		dev_tab[std_dev[i]] = &bad_dev;
	for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* add devices */
		dibp = (DIB *)dptr->ctxt;                          /* get DIB */
		if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
			for (j = 0; j < dibp->num; j++) {               /* loop thru disp */
				if (dibp->dsp[j]) {                         /* any dispatch? */
					if (dev_tab[dibp->dev + j]) {           /* already filled? */
						sim_printf("%s device number conflict at %02o\n",
							sim_dname(dptr), dibp->dev + j);
						return TRUE;
					}
					dev_tab[dibp->dev + j] = dibp->dsp[j];  /* fill */
				}                                       /* end if dsp */
			}                                           /* end for j */
		}                                               /* end if enb */
	}                                                   /* end for i */
	return FALSE;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
	int32 i, lnt;
	t_stat r;

	if (cptr == NULL) {
		for (i = 0; i < hst_lnt; i++)
			hst[i].pc = 0;
		hst_p = 0;
		return SCPE_OK;
	}
	lnt = (int32)get_uint(cptr, 10, HIST_MAX, &r);
	if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
		return SCPE_ARG;
	hst_p = 0;
	if (hst_lnt) {
		free(hst);
		hst_lnt = 0;
		hst = NULL;
	}
	if (lnt) {
		hst = (InstHistory *)calloc(lnt, sizeof(InstHistory));
		if (hst == NULL)
			return SCPE_MEM;
		hst_lnt = lnt;
	}
	return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
	int32 l, k, di, lnt;
const char *cptr = (const char *) desc;
	t_stat r;
	t_value sim_eval;
	InstHistory *h;

	if (hst_lnt == 0)                                       /* enabled? */
		return SCPE_NOFNC;
	if (cptr) {
		lnt = (int32)get_uint(cptr, 10, hst_lnt, &r);
		if ((r != SCPE_OK) || (lnt == 0))
			return SCPE_ARG;
	}
	else lnt = hst_lnt;
	di = hst_p - lnt;                                       /* work forward */
	if (di < 0)
		di = di + hst_lnt;
	fprintf(st, "PC     L AC    MQ    ea     IR\n\n");
	for (k = 0; k < lnt; k++) {                             /* print specified */
		h = &hst[(++di) % hst_lnt];                         /* entry pointer */
		if (h->pc & HIST_PC) {                              /* instruction? */
			l = (h->lac >> 12) & 1;                         /* link */
			fprintf(st, "%05o  %o %04o  %04o  ", h->pc & ADDRMASK, l, h->lac & 07777, h->mq);
			if (h->ir < 06000)
				fprintf(st, "%05o  ", h->ea);
			else fprintf(st, "       ");
			sim_eval = h->ir;
			if ((fprint_sym(st, h->pc & ADDRMASK, &sim_eval, &cpu_unit, SWMASK('M'))) > 0)
				fprintf(st, "(undefined) %04o", h->ir);
			if (h->ir < 04000)
				fprintf(st, "  [%04o]", h->opnd);
			fputc('\n', st);                               /* end line */
		}                                               /* end else instruction */
	}                                                   /* end for */
	return SCPE_OK;
}
