/* main.c: Blinkenlight API server to run on "PiDP8" replica

   Copyright (c) 2015-2016, Joerg Hoppe
   j_hoppe@t-online.de, www.retrocmp.com

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
   JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


   22-Feb-2016  JH		V 1.1 added panel modes LAMPTEST, POWERLESS
   13-Nov-2015  JH      V 1.0 created


   Blinkenlight API server, which controls lamps and switches
   on the Raspberry based "PiPDP8 replica from Oscar Vermeulen.

   Like the generic blinkenlightd,
     - PiDP8 cobntrols are fix wired in, no config file
     - Hardware interface to Raspberry is original "gpio.c" from Oscar

   DEC names for switches and LEDs : see
       PDP-8 FAMILY SYSTEM USER'S GUIDE
       dec-08-ngcb-d-.pdf, pdf;  page INTRO-5, pdpf page 19

*/


#define MAIN_C_


#define VERSION	"v1.1.0"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>

#include "blinkenlight_panels.h"
#include "blinkenlight_api_server_procs.h"

///// for Blinkenlight API server
#include "rpc_blinkenlight_api.h"
#include <rpc/pmap_clnt.h> // different name under "oncrpc for windows"?
#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif

#include "bitcalc.h"
#include "print.h"

#include "main.h"
#include "gpio.h"

char program_info[1024];
char program_name[1024]; // argv[0]
char program_options[1024]; // argv[1.argc-1]

/*
 *	PiDP8 controls wich are accessible over Blinkenlight API
 */
blinkenlight_control_t *control_raw_switchstatus[2];
blinkenlight_control_t *control_raw_ledstatus[8];

blinkenlight_control_t *switch_start;
blinkenlight_control_t *switch_load_address;
blinkenlight_control_t *switch_deposit;
blinkenlight_control_t *switch_examine;
blinkenlight_control_t *switch_continue;
blinkenlight_control_t *switch_stop;
blinkenlight_control_t *switch_single_step;
blinkenlight_control_t *switch_single_instruction;
blinkenlight_control_t *switch_switch_register;
blinkenlight_control_t *switch_data_field;
blinkenlight_control_t *switch_instruction_field;

blinkenlight_control_t *keyswitch_power; // dummy
blinkenlight_control_t *keyswitch_panel_lock; // dummy

blinkenlight_control_t *led_program_counter;
blinkenlight_control_t *led_inst_field;
blinkenlight_control_t *led_data_field;
blinkenlight_control_t *led_memory_address;
blinkenlight_control_t *led_memory_buffer;
blinkenlight_control_t *led_accumulator;
blinkenlight_control_t *led_link;
blinkenlight_control_t *led_multiplier_quotient;
blinkenlight_control_t *led_and;
blinkenlight_control_t *led_tad;
blinkenlight_control_t *led_isz;
blinkenlight_control_t *led_dca;
blinkenlight_control_t *led_jms;
blinkenlight_control_t *led_jmp;
blinkenlight_control_t *led_iot;
blinkenlight_control_t *led_opr;
blinkenlight_control_t *led_fetch;
blinkenlight_control_t *led_execute;
blinkenlight_control_t *led_defer;
blinkenlight_control_t *led_word_count;
blinkenlight_control_t *led_current_address;
blinkenlight_control_t *led_break;
blinkenlight_control_t *led_ion;
blinkenlight_control_t *led_pause;
blinkenlight_control_t *led_run;
blinkenlight_control_t *led_step_counter;

/*
 *	RPC server callbacks:
 *	here conversion between PiDP8 gpio and Blinkenlight API is done!
 */
static void on_blinkenlight_api_panel_get_controlvalues(blinkenlight_panel_t *p)
{
	// gets called when RPC client wants panel input control values
	//- converts gpio switches to Blinkenlight API switch conrols (RPI->Blinkenlight API)
	unsigned i;
	for (i = 0; i < p->controls_count; i++) {
		blinkenlight_control_t *c = &p->controls[i];
		if (c->is_input) {
			if (c == keyswitch_power)
				c->value = 1; // send "power"" switch as ON
			else if (c == keyswitch_panel_lock)
				c->value = 0; // send "panel lock" switch as OFF
			else {
				// fetch gpio bits, shift and mask
				unsigned gpio_register_idx =
						c->blinkenbus_register_wiring[0].blinkenbus_register_address;
				unsigned gpio_bits = ~gpio_switchstatus[gpio_register_idx];
				unsigned bit_offset = c->blinkenbus_register_wiring[0].blinkenbus_lsb;
				unsigned mask = BitmaskFromLen32[c->value_bitlen];
				c->value = (gpio_bits >> bit_offset) & mask; // all bits inverted
			}
		}
	}
}

static void on_blinkenlight_api_panel_set_controlvalues(blinkenlight_panel_t *p, int force_all)
{
	// gets called when RPC client updated panel control values
	// converts Blinkenlight API leds to gpio (Blinkenligt API -> RPI)
	unsigned i;
	// mount values for gpio_registers ordered by register,
	// else flicker by co-running gpio_mux may occur.

	for (i = 0; i < p->controls_count; i++) {
		blinkenlight_control_t *c = &p->controls[i];
		if (!c->is_input) {
			// fetch  shift
			unsigned gpio_register_idx =
					c->blinkenbus_register_wiring[0].blinkenbus_register_address;
			unsigned bit_offset = c->blinkenbus_register_wiring[0].blinkenbus_lsb;
			unsigned mask = (BitmaskFromLen32[c->value_bitlen] << bit_offset);
			unsigned gpio_bits = (c->value << bit_offset);
			switch (p->mode) {
			case RPC_PARAM_VALUE_PANEL_MODE_NORMAL:
				gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask)
						| gpio_bits;
			break ;
			case RPC_PARAM_VALUE_PANEL_MODE_LAMPTEST:
				// all LEDs on, but do not change control values
				gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask) | mask ;
			break ;
			case RPC_PARAM_VALUE_PANEL_MODE_ALLTEST:
				// all LEDs on, but do not change control values
				gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask) | mask ;
			break ;
			case RPC_PARAM_VALUE_PANEL_MODE_POWERLESS:
				// all LEDs off, but do not change control values
				gpio_ledstatus[gpio_register_idx] = 0 ;
			break ;
			}
			}
		}
//	for (i = 0; i < 8; i++)
//		gpio_ledstatus[i] = control_raw_ledstatus[i]->value;

}

static int on_blinkenlight_api_panel_get_state(blinkenlight_panel_t *p)
{
	// dummy: "all BlinkenBoards of panel active"
	return RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE;
}

static void on_blinkenlight_api_panel_set_state(blinkenlight_panel_t *p, int new_state)
{
	// noop
}


// set get selftest/powerless mode
static int on_blinkenlight_api_panel_get_mode(blinkenlight_panel_t *p)
{
	return p->mode ;
}

static void on_blinkenlight_api_panel_set_mode(blinkenlight_panel_t *p, int new_state)
{
	p->mode = new_state ;
	// GPIO logic here
	on_blinkenlight_api_panel_set_controlvalues(p, 1) ;
}


static char *on_blinkenlight_api_get_info()
{
	static char buffer[1024];

	sprintf(buffer, "Server info ...............: %s\n"
			"Server program name........: %s\n"
			"Server command line options: %s\n"
			"Server compile time .......: " __DATE__ " " __TIME__ "\n", //
			program_info, program_name, program_options);

	return buffer;
}

/*
 * Start the parallel thread which operates the GPIO mux.
 */
void *blink(void *ptr); // the real-time multiplexing process to start up
pthread_t blink_thread;
int blink_thread_terminate = 0;

static void start_gpio_mux_thread()
{
// ------------------------------------------------------------------------
// PiDP8 hack here
	// const char *message = "Thread 1";
	int iret1;

//	printf("\nPiDP FP driver 3\n");

	//blinky(NULL) ;
	// create thread
	iret1 = pthread_create(&blink_thread, NULL, blink, &blink_thread_terminate);

	if (iret1) {
		fprintf(stderr, "Error creating thread, return code %d\n", iret1);
		exit(EXIT_FAILURE);
	}
//	printf("Created thread, return code %d\n", iret1);

	sleep(2); // allow 2 sec for multiplex to start
// ------------------------------------------------------------------------
}

/******************************************************
 * Server for Blinkenlight API
 * see code generated by rpcgen
 * DOES NEVER END!
 */
void blinkenlight_api_server(void)
{
	register SVCXPRT *transp;

// entry to server stub

	void blinkenlightd_1(struct svc_req *rqstp, register SVCXPRT *transp);

	pmap_unset(BLINKENLIGHTD, BLINKENLIGHTD_VERS);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		print(LOG_ERR, "%s", "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, BLINKENLIGHTD, BLINKENLIGHTD_VERS, blinkenlightd_1, IPPROTO_UDP)) {
		print(LOG_ERR, "%s", "unable to register (BLINKENLIGHTD, BLINKENLIGHTD_VERS, udp).");
		exit(1);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		print(LOG_ERR, "%s", "cannot create tcp service.");
		exit(1);
	}
	if (!svc_register(transp, BLINKENLIGHTD, BLINKENLIGHTD_VERS, blinkenlightd_1, IPPROTO_TCP)) {
		print(LOG_ERR, "%s", "unable to register (BLINKENLIGHTD, BLINKENLIGHTD_VERS, tcp).");
		exit(1);
	}

	// svc_run();
	// alternate implementation of svn_run() with periodically timeout and
	// 	calling of callback
	{
		fd_set readfds;
		struct timeval tv;
		int dtbsz = getdtablesize();
		for (;;) {
			readfds = svc_fdset;
			tv.tv_sec = 0;
			tv.tv_usec = 1000 * 2; // every 10 ms*time_slice_ms;
			switch (select(dtbsz, &readfds, NULL, NULL, &tv)) {
			case -1:
				if (errno == EINTR)
					continue;
				perror("select");
				return;
			case 0: // timeout
					// provide the panel simulation with computing time
				// not needed:RPC calls control value get/set callbacks
				break;
			default:
				svc_getreqset(&readfds);
				break;
			}
			/**/
		}
	}

	print(LOG_ERR, "%s", "svc_run returned");
	exit(1);
	/* NOTREACHED */

}

/*
 * print program info
 */
void info(void)
{
	print(LOG_INFO, "\n");
	print(LOG_NOTICE, "*** blinkenlightd %s - server for PiDP8 ***\n", VERSION);
	print(LOG_NOTICE, "    Compiled " __DATE__ " " __TIME__ "\n");
	print(LOG_NOTICE, "    Copyright (C) 2015-2016 Joerg Hoppe, Oscar Vermeulen.\n");
	print(LOG_NOTICE, "    Contact: j_hoppe@t-online.de, www.retrocmp.com\n");
	print(LOG_NOTICE, "\n");
}

/*
 * print help
 */
static void help(void)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "blinkenlightd %s - Blinkenlight RPC server for PiDP8 \n",
	VERSION);
	fprintf(stderr, "  (compiled " __DATE__ " " __TIME__ ")\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Call:\n");
	fprintf(stderr, "blinkenlightd [-b] [-v]\n");
	fprintf(stderr, "\n");
//	fprintf(stderr, "- <port>:               TCP port for RCP access.\n") ;
	fprintf(stderr, "-b               : background operation: print to syslog (view with dmesg)\n");
	fprintf(stderr, "                  default output is stderr\n");
	fprintf(stderr, "-v               : verbose: tell what I'm doing\n");
	fprintf(stderr, "\n");
}

/*
 * read commandline paramaters into global vars
 * result: 1 = OK, 0 = error
 */
static int parse_commandline(int argc, char **argv)
{
	int i;
	int c;
	int opt_background = 0;

	strcpy(program_name, argv[0]);
	strcpy(program_options, "");
	for (i = 1; i < argc; i++) {
		if (i > 1)
			strcat(program_options, " ");
		strcat(program_options, argv[i]);
	}

	opterr = 0;

	while ((c = getopt(argc, argv, "bv")) != -1)
		switch (c) {
		case 'v':
			print_level = LOG_DEBUG;
			break;
		case 'b':
			opt_background = 1;
			break;
		case '?': // getopt detected an error. "opterr=0", so own error message here
			if (isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
			return 0;
			break;
		default:
			abort(); // getopt() got crazy?
			break;
		}

	// start logging
	print_open(opt_background); // if background, then syslog

//     for (index = optind; index < argc; index++)
//       printf ("Non-option argument %s\n", argv[index]);
//     return 0;
	return 1;
}

/*
 * Define the fix controls of the PiDP8 == PDP8/I
 * for the Blinkenlight API interface structs
 */
static blinkenlight_control_t *register_switch(blinkenlight_panel_t *p, char *name, unsigned bitlen,
		unsigned statusregister_index, unsigned bit_offset)
{
	blinkenlight_control_t *c;

	c = blinkenlight_add_control(blinkenlight_panel_list, p);
	strcpy(c->name, name);
	c->is_input = 1;
	c->type = input_switch;
	c->encoding = binary;
	c->radix = 8; // display octal
	c->value_bitlen = bitlen;
	// round bitlen up to bytes
	c->value_bytelen = (c->value_bitlen + 7) / 8; // 0-> 0, 1->1 8->1, 9->2, ...

	// shift and mask data are saved in part of the "register wriring" struct.
	c->blinkenbus_register_wiring_count = 1;
	c->blinkenbus_register_wiring[0].blinkenbus_register_address = statusregister_index;
	c->blinkenbus_register_wiring[0].blinkenbus_lsb = bit_offset;

	// update panel
	p->controls_inputs_count++;
	p->controls_inputs_values_bytecount += c->value_bytelen;

	return c;
}

static blinkenlight_control_t *register_led(blinkenlight_panel_t *p, char *name, unsigned bitlen,
		unsigned statusregister_index, unsigned bit_offset)
{
	blinkenlight_control_t *c;
	c = blinkenlight_add_control(blinkenlight_panel_list, p);
	strcpy(c->name, name);
	c->is_input = 0;
	c->type = output_lamp;
	c->encoding = binary;
	c->radix = 8; // display octal
	c->value_bitlen = bitlen;
	// round bitlen up to bytes
	c->value_bytelen = (c->value_bitlen + 7) / 8; // 0-> 0, 1->1 8->1, 9->2, ...

	// shift and mask data are saved in part of the "register wriring" struct.
	c->blinkenbus_register_wiring_count = 1;
	c->blinkenbus_register_wiring[0].blinkenbus_register_address = statusregister_index;
	c->blinkenbus_register_wiring[0].blinkenbus_lsb = bit_offset;

	// update panel
	p->controls_outputs_count++;
	p->controls_outputs_values_bytecount += c->value_bytelen;

	return c;
}

static void register_controls()
{
	blinkenlight_panel_t *p;

	// one list
	blinkenlight_panel_list = blinkenlight_panels_constructor();

	// one panel
	p = blinkenlight_add_panel(blinkenlight_panel_list);
	strcpy(p->name, "PiDP8");
	strcpy(p->info, "PiDP8 replica made by Oscar Vermeulen");

	// add raw controls
	// Defintions in gpio.c
	// uint32 switchstatus[3] = { 0 }; // bitfields: 3 rows of up to 12 switches
	// uint32 ledstatus[8] = { 0 };	// bitfields: 8 ledrows of up to 12 LEDs
	// Necessary initalisations see ANT grammar

	/*
	 // the raw registers for gpio mux
	 control_raw_switchstatus[0] = register_switch(p, "switchstatus[0]", 32, 0, 0);
	 control_raw_switchstatus[1] = register_switch(p, "switchstatus[1]", 32, 1, 0);
	 control_raw_switchstatus[2] = register_switch(p, "switchstatus[2]", 32, 2, 0);
	 control_raw_ledstatus[0] = register_led(p, "ledstatus[0]", 32, 0, 0);
	 control_raw_ledstatus[1] = register_led(p, "ledstatus[1]", 32, 1, 0);
	 control_raw_ledstatus[2] = register_led(p, "ledstatus[2]", 32, 2, 0);
	 control_raw_ledstatus[3] = register_led(p, "ledstatus[3]", 32, 3, 0);
	 control_raw_ledstatus[4] = register_led(p, "ledstatus[4]", 32, 4, 0);
	 control_raw_ledstatus[5] = register_led(p, "ledstatus[5]", 32, 5, 0);
	 control_raw_ledstatus[6] = register_led(p, "ledstatus[6]", 32, 6, 0);
	 control_raw_ledstatus[7] = register_led(p, "ledstatus[7]", 32, 7, 0);
	 /**/

	// alle switches invers: bit 1 => switch up in "0" position
	//  "2", 0x400": switchstatus[2], mask 0x400
	switch_start = register_switch(p, "Start", 1, 2, 11); // 2, 0x800
	switch_load_address = register_switch(p, "Load Add", 1, 2, 10); // 2, 0x400
	switch_deposit = register_switch(p, "Dep", 1, 2, 9); // 2, 0x200
	switch_examine = register_switch(p, "Exam", 1, 2, 8); // 2, 0x100
	switch_continue = register_switch(p, "Cont", 1, 2, 7); // 2, 0x80
	switch_stop = register_switch(p, "Stop", 1, 2, 6); // 2, 0x40
	switch_single_step = register_switch(p, "Sing Step", 1, 2, 5); // 2, 0x20
	switch_single_instruction = register_switch(p, "Sing Inst", 1, 2, 4); // 2, 0x10
	switch_switch_register = register_switch(p, "SR", 12, 0, 0); // 0, 0xfff
	switch_data_field = register_switch(p, "DF", 3, 1, 9); // 1, 0xe00
	switch_instruction_field = register_switch(p, "IF", 3, 1, 6); // 1, 0x1c0

	keyswitch_power = register_switch(p, "POWER", 1, 0, 0); // dummy, always 1
	keyswitch_panel_lock = register_switch(p, "PANEL LOCK", 1, 0, 0); // dummy, always 0
	// Constant values set in on_blinkenlight_api_panel_get_controlvalues()

	// "1, 0x30" -> ledstatus[0], mask 0x30
	led_program_counter = register_led(p, "Program Counter", 12, 0, 0); // 0, 0xfff
	led_inst_field = register_led(p, "Inst Field", 3, 7, 6); // 7, 0x1c0
	led_data_field = register_led(p, "Data Field", 3, 7, 9); // 7, 0xe00
	led_memory_address = register_led(p, "Memory Address", 12, 1, 0); // 1, 0xfff
	led_memory_buffer = register_led(p, "Memory Buffer", 12, 2, 0); // 2, 0xfff
	led_accumulator = register_led(p, "Accumulator", 12, 3, 0); // 3, 0xfff
	led_link = register_led(p, "Link", 1, 7, 5); // 7, 0x20
	led_multiplier_quotient = register_led(p, "Multiplier Quotient", 12, 4, 0); // 4, 0xfff
	led_and = register_led(p, "And", 1, 5, 11); // 5, 0x800
	led_tad = register_led(p, "Tad", 1, 5, 10); // 5, 0x400
	led_isz = register_led(p, "Isz", 1, 5, 9); // 5, 0x200
	led_dca = register_led(p, "Dca", 1, 5, 8); // 5, 0x100
	led_jms = register_led(p, "Jms", 1, 5, 7); // 5, 0x80
	led_jmp = register_led(p, "Jmp", 1, 5, 6); // 5, 0x40
	led_iot = register_led(p, "Iot", 1, 5, 5); // 5, 0x20
	led_opr = register_led(p, "Opr", 1, 5, 4); // 5, 0x10
	led_fetch = register_led(p, "Fetch", 1, 5, 3); // 5, 0x8
	led_execute = register_led(p, "Execute", 1, 5, 2); // 5, 0x4
	led_defer = register_led(p, "Defer", 1, 5, 1); // 5, 0x2
	led_word_count = register_led(p, "Word Count", 1, 5, 0); // 5, 0x1
	led_current_address = register_led(p, "Current Address", 1, 6, 11); // 6, 0x800
	led_break = register_led(p, "Break", 1, 6, 10); // 6, 0x400
	led_ion = register_led(p, "Ion", 1, 6, 9); // 6, 0x200
	led_pause = register_led(p, "Pause", 1, 6, 8); // 6, 0x100
	led_run = register_led(p, "Run", 1, 6, 7); // 6, 0x80
	led_step_counter = register_led(p, "Step Counter", 5, 6, 2); // 6,  0x7c
}

/*
 *
 */
int main(int argc, char *argv[])
{

	print_level = LOG_NOTICE;
	// print_level = LOG_DEBUG;
	if (!parse_commandline(argc, argv)) {
		help();
		return 1;
	}
	sprintf(program_info, "blinkenlightd - Blinkenlight API server daemon for PiDP8 %s", VERSION);

	info();

	/*
	 {struct sched_param sp;
	 sp.sched_priority = 10; // maybe 99, 32, 31?
	 if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp))
	 fprintf(stderr, "warning: failed to set RT priority\n");
	 }
	 */
	print(LOG_INFO, "Start\n");

	// link set/get events
	blinkenlight_api_panel_get_controlvalues_evt = on_blinkenlight_api_panel_get_controlvalues;
	blinkenlight_api_panel_set_controlvalues_evt = on_blinkenlight_api_panel_set_controlvalues;
	blinkenlight_api_panel_get_state_evt = on_blinkenlight_api_panel_get_state;
	blinkenlight_api_panel_set_state_evt = on_blinkenlight_api_panel_set_state;
	blinkenlight_api_panel_get_mode_evt = on_blinkenlight_api_panel_get_mode;
	blinkenlight_api_panel_set_mode_evt = on_blinkenlight_api_panel_set_mode;
	blinkenlight_api_get_info_evt = on_blinkenlight_api_get_info;

	register_controls();
	start_gpio_mux_thread();

	/** /
	 {
	 int i, j;
	 //		for (j = 0; j < 100000000; j++)
	 for (i = 0; i < 8; i++)
	 gpio_ledstatus[i] = 0x11111111;
	 //		return 0;
	 }
	 /**/


	blinkenlight_api_server();
	// does never end!

	print_close(); // well ....

	blinkenlight_panels_destructor(blinkenlight_panel_list);

	return 0;
}
