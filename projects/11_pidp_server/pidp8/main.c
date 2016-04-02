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


 22-Mar-2016  JH    allow a control value to be distributed over several hw registers
 15-Mar-2016  JH	V 1.3 Low-pass for SimH output, display patterns for brightness levels
 09-Mar-2016  JH	V 1.2 inverted "Deposit" switch
 06-Mar-2016  JH    renamed from "blinkenlightd" to "pidp8_blinkenlightd"
 22-Feb-2016  JH	V 1.1 added panel modes LAMPTEST, POWERLESS
 13-Nov-2015  JH    V 1.0 created


 Blinkenlight API server, which controls lamps and switches
 on the Raspberry based "PiPDP8 replica from Oscar Vermeulen.

 Like the generic blinkenlightd,
 - PiDP8 controls are fix wired in, no config file
 - Hardware interface to Raspberry is original "gpio.c" from Oscar

 DEC names for switches and LEDs : see
 PDP-8 FAMILY SYSTEM USER'S GUIDE
 dec-08-ngcb-d-.pdf, pdf;  page INTRO-5, pdpf page 19


 Timing & CPU load:
 There are 2 threads:
 a) the LED MUX loop, in gpio.c, long intervl
 b) the averaging loop in gpiopattern.c
 (and SimH is the 3rd process running)

 To fine trim cpu load, use web based "rCPU"
 https://github.com/davidsblog/rCPU

 */

#define MAIN_C_

#define VERSION	"v1.3.0"

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
#include "gpiopattern.h"

char program_info[1024];
char program_name[1024]; // argv[0]
char program_options[1024]; // argv[1.argc-1]
int opt_test = 0;
int opt_background = 0;

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
                // mount switch value from register bit fields
                unsigned i_register_wiring;
                blinkenlight_control_blinkenbus_register_wiring_t *bbrw;
                c->value = 0;
                for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
                        i_register_wiring++) {
                    uint32_t regvalbits; // value of current register
                    // for all registers assigned whole or in part to control
                    bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);

                    regvalbits = gpio_switchstatus[bbrw->blinkenbus_register_address];
                    if (bbrw->blinkenbus_levels_active_low) //  inputs "low active"
                        regvalbits = ~regvalbits;
                    regvalbits &= bbrw->blinkenbus_bitmask; // bits of value, unshiftet
                    regvalbits >>= bbrw->blinkenbus_lsb;
                    // OR in the bits of current register
                    c->value |= (uint64_t) regvalbits << bbrw->control_value_bit_offset;
                }
                if (c->mirrored_bit_order)
                    c->value = mirror_bits(c->value, c->value_bitlen);

                // individual fixup/logic
                // the deposit switch must be inverted, PiDP8 electronics doesn't handle this
                if (c == switch_deposit)
                    c->value = !c->value;
            }
        }
    }
}

static void on_blinkenlight_api_panel_set_controlvalues(blinkenlight_panel_t *p, int force_all)
{
    // gets called when RPC client updated panel control values
    // converts Blinkenlight API leds to gpio (Blinkenligt API -> RPI)

    // the averaging thread needs to be informed about the panel
    // THIS WORKS ONLY BECAUSE ONLY ONE PANEL is provided by this server!
    // NO PANEL SWITCH ALLOWED!
    gpiopattern_blinkenlight_panel = p;
    // this also start the thread on first transmission, if gpiopattern_blinkenlight_panel gets != NULL

    // gpiopattern_update_leds() ; // just forward to pattern generator
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
    return p->mode;
}

static void on_blinkenlight_api_panel_set_mode(blinkenlight_panel_t *p, int new_state)
{
    p->mode = new_state;
    // GPIO logic here
    on_blinkenlight_api_panel_set_controlvalues(p, 1);
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
void *blink(void *ptr); // the real-time GPIO multiplexing process to start up
void *gpiopattern_update_leds(void *ptr); // the averaging thread

pthread_t blink_thread;
int blink_thread_terminate = 0;
pthread_t gpiopattern_thread;
int gpiopattern_thread_terminate = 0;

static void gpio_mux_thread_start()
{
    int res;
//	printf("\nPiDP FP driver 3\n");
    res = pthread_create(&blink_thread, NULL, blink, &blink_thread_terminate);
    if (res) {
        fprintf(stderr, "Error creating gpio_mux thread, return code %d\n", res);
        exit(EXIT_FAILURE);
    }
    printf("Created \"gpio_mux\" thread\n");

    sleep(2); // allow 2 sec for multiplex to start
}

static void gpiopattern_start_thread()
{
    int res;
    gpiopattern_blinkenlight_panel = NULL; // wait for first API transmission to start

    res = pthread_create(&gpiopattern_thread, NULL, gpiopattern_update_leds,
            &gpiopattern_thread_terminate);
    if (res) {
        fprintf(stderr, "Error creating gpiopattern thread, return code %d\n", res);
        exit(EXIT_FAILURE);
    }
    printf("Created \"gpiopattern_update_leds\" thread\n");
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
    print(LOG_NOTICE, "*** pidp8_blinkenlightd %s - server for PiDP8 ***\n", VERSION);
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
    fprintf(stderr, "pidp8_blinkenlightd %s - Blinkenlight RPC server for PiDP8 \n",
    VERSION);
    fprintf(stderr, "  (compiled " __DATE__ " " __TIME__ ")\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Call:\n");
    fprintf(stderr, "pidp8_blinkenlightd [-b] [-v] [-t]\n");
    fprintf(stderr, "\n");
//	fprintf(stderr, "- <port>:               TCP port for RCP access.\n") ;
    fprintf(stderr, "-b               : background operation: print to syslog (view with dmesg)\n");
    fprintf(stderr, "                  default output is stderr\n");
    fprintf(stderr, "-v               : verbose: tell what I'm doing\n");
    fprintf(stderr, "-t               : Test mode\n");
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

    strcpy(program_name, argv[0]);
    strcpy(program_options, "");
    for (i = 1; i < argc; i++) {
        if (i > 1)
            strcat(program_options, " ");
        strcat(program_options, argv[i]);
    }

    opterr = 0;

    while ((c = getopt(argc, argv, "bvt")) != -1)
        switch (c) {
        case 'v':
            print_level = LOG_DEBUG;
            break;
        case 'b':
            opt_background = 1;
            break;
        case 't':
            opt_test = 1;
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
 * Define part of the fix controls of the PiDP11 == PDP11/70
 * for the Blinkenlight API interface structs
 * - control_value_bit_offset: position of bitfield in final control value
 * - bitlen: count of bits in status register to be mounted into value
 * - gpio_switchstatus_index: gpio_switchstatus[idx] is mounted here
 * - bit_offset: LSB of bitfield in gpio_switchstatus[] to be shifted this much
 *
 * See http://retrocmp.com/projects/blinkenbone/blinkenbone-physical-panels/173-blinkenbone-blinkenlightd-patch-field-decoding-and-console-panel-simulator
 */
static blinkenlight_control_t *define_switch_slice(blinkenlight_panel_t *p, char *name,
        unsigned control_value_bit_offset, unsigned bitlen, unsigned gpio_switchstatus_index,
        unsigned bit_offset)
{
    blinkenlight_control_t *c;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;

    // control already there?
    c = blinkenlight_panels_get_control_by_name(blinkenlight_panel_list, p, name, /*is_input*/1);
    if (c == NULL) {
        c = blinkenlight_add_control(blinkenlight_panel_list, p);
        strcpy(c->name, name);
        c->is_input = 1;
        c->type = input_switch;
        c->encoding = binary;
        c->radix = 8; // display octal
    }
    // shift and mask data are saved in the "register wiring" struct.
    bbrw = blinkenlight_add_register_wiring(c);
    bbrw->blinkenbus_board_address = 0; // simulate 1 board with unlimited registers
    bbrw->board_register_address = gpio_switchstatus_index;
    bbrw->control_value_bit_offset = control_value_bit_offset;
    bbrw->blinkenbus_lsb = bit_offset;
    bbrw->blinkenbus_msb = bbrw->blinkenbus_lsb + bitlen - 1;
    // all switches invers: bit 1 => switch up in "0" position
    bbrw->blinkenbus_levels_active_low = 1;

    return c;
}

// define outputs, see register_switch_slice()
static blinkenlight_control_t *define_lamp_slice(blinkenlight_panel_t *p, char *name,
        unsigned control_value_bit_offset, unsigned bitlen, unsigned gpio_ledstatus_index,
        unsigned bit_offset)
{
    blinkenlight_control_t *c;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;

    c = blinkenlight_panels_get_control_by_name(blinkenlight_panel_list, p, name, /*is_input*/0);
    if (c == NULL) {
        c = blinkenlight_add_control(blinkenlight_panel_list, p);
        strcpy(c->name, name);
        c->is_input = 0;
        c->type = output_lamp;
        c->encoding = binary;
        c->fmax = 4; // lamps are slow light bulbs, low-pass with 4 Hz
        c->radix = 8; // display octal
    }
    bbrw = blinkenlight_add_register_wiring(c);
    bbrw->blinkenbus_board_address = 0; // simulate 1 board with unlimited registers
    bbrw->board_register_address = gpio_ledstatus_index;
    bbrw->control_value_bit_offset = control_value_bit_offset;
    bbrw->blinkenbus_lsb = bit_offset;
    bbrw->blinkenbus_msb = bbrw->blinkenbus_lsb + bitlen - 1;
    bbrw->blinkenbus_levels_active_low = 0; // LED drivers not inverting

    return c;
}

static void register_controls()
{
    blinkenlight_panel_t *p;

    // one global panel list ...
    blinkenlight_panel_list = blinkenlight_panels_constructor();
    // ... with one panel
    p = blinkenlight_add_panel(blinkenlight_panel_list);
    strcpy(p->name, "PiDP8");
    strcpy(p->info, "PiDP8 replica made by Oscar Vermeulen");

    /*
     * Construct high-level Blinkenlight API controls from
     * hardware switch- and led-registers
     */

    switch_start = define_switch_slice(p, "Start", 0, 1, 2, 11); // 2, 0x800
    switch_load_address = define_switch_slice(p, "Load Add", 0, 1, 2, 10); // 2, 0x400
    switch_deposit = define_switch_slice(p, "Dep", 0, 1, 2, 9); // 2, 0x200
    switch_examine = define_switch_slice(p, "Exam", 0, 1, 2, 8); // 2, 0x100
    switch_continue = define_switch_slice(p, "Cont", 0, 1, 2, 7); // 2, 0x80
    switch_stop = define_switch_slice(p, "Stop", 0, 1, 2, 6); // 2, 0x40
    switch_single_step = define_switch_slice(p, "Sing Step", 0, 1, 2, 5); // 2, 0x20
    switch_single_instruction = define_switch_slice(p, "Sing Inst", 0, 1, 2, 4); // 2, 0x10
    switch_switch_register = define_switch_slice(p, "SR", 0, 12, 0, 0); // 0, 0xfff
    switch_data_field = define_switch_slice(p, "DF", 0, 3, 1, 9); // 1, 0xe00
    switch_instruction_field = define_switch_slice(p, "IF", 0, 3, 1, 6); // 1, 0x1c0

    keyswitch_power = define_switch_slice(p, "POWER", 0, 1, 0, 0); // dummy, always 1
    keyswitch_panel_lock = define_switch_slice(p, "PANEL LOCK", 0, 1, 0, 0); // dummy, always 0
    // Constant values set in on_blinkenlight_api_panel_get_controlvalues()

    // Fun: define pc in two slices:  bits 11:7, 6:0
    led_program_counter = define_lamp_slice(p, "Program Counter", 0, 7, 0, 0); // bits 0:6
    led_program_counter = define_lamp_slice(p, "Program Counter", 7, 5, 0, 7); // bits 7:11
    // led_program_counter = register_lamp_slice(p, "Program Counter", 0, 12, 0, 0); // 0, 0xfff

    led_inst_field = define_lamp_slice(p, "Inst Field", 0, 3, 7, 6); // 7, 0x1c0
    led_data_field = define_lamp_slice(p, "Data Field", 0, 3, 7, 9); // 7, 0xe00
    led_memory_address = define_lamp_slice(p, "Memory Address", 0, 12, 1, 0); // 1, 0xfff
    led_memory_buffer = define_lamp_slice(p, "Memory Buffer", 0, 12, 2, 0); // 2, 0xfff
    led_accumulator = define_lamp_slice(p, "Accumulator", 0, 12, 3, 0); // 3, 0xfff
    led_link = define_lamp_slice(p, "Link", 0, 1, 7, 5); // 7, 0x20
    led_multiplier_quotient = define_lamp_slice(p, "Multiplier Quotient", 0, 12, 4, 0); // 4, 0xfff
    led_and = define_lamp_slice(p, "And", 0, 1, 5, 11); // 5, 0x800
    led_tad = define_lamp_slice(p, "Tad", 0, 1, 5, 10); // 5, 0x400
    led_isz = define_lamp_slice(p, "Isz", 0, 1, 5, 9); // 5, 0x200
    led_dca = define_lamp_slice(p, "Dca", 0, 1, 5, 8); // 5, 0x100
    led_jms = define_lamp_slice(p, "Jms", 0, 1, 5, 7); // 5, 0x80
    led_jmp = define_lamp_slice(p, "Jmp", 0, 1, 5, 6); // 5, 0x40
    led_iot = define_lamp_slice(p, "Iot", 0, 1, 5, 5); // 5, 0x20
    led_opr = define_lamp_slice(p, "Opr", 0, 1, 5, 4); // 5, 0x10
    led_fetch = define_lamp_slice(p, "Fetch", 0, 1, 5, 3); // 5, 0x8
    led_execute = define_lamp_slice(p, "Execute", 0, 1, 5, 2); // 5, 0x4
    led_defer = define_lamp_slice(p, "Defer", 0, 1, 5, 1); // 5, 0x2
    led_word_count = define_lamp_slice(p, "Word Count", 0, 1, 5, 0); // 5, 0x1
    led_current_address = define_lamp_slice(p, "Current Address", 0, 1, 6, 11); // 6, 0x800
    led_break = define_lamp_slice(p, "Break", 0, 1, 6, 10); // 6, 0x400
    led_ion = define_lamp_slice(p, "Ion", 0, 1, 6, 9); // 6, 0x200
    led_pause = define_lamp_slice(p, "Pause", 0, 1, 6, 8); // 6, 0x100
    led_run = define_lamp_slice(p, "Run", 0, 1, 6, 7); // 6, 0x80
    led_step_counter = define_lamp_slice(p, "Step Counter", 0, 5, 6, 2); // 6,  0x7c

    // calculate dependent values
    blinkenlight_panels_config_fixup(blinkenlight_panel_list);
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
    sprintf(program_info, "pidp8_blinkenlightd - Blinkenlight API server daemon for PiDP8 %s",
    VERSION);

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

    if (opt_test) {
        printf("Dump of register <-> control data struct:\n");
        blinkenlight_panels_diagprint(blinkenlight_panel_list, stdout);
        exit(0);
    }

    gpio_mux_thread_start();
    gpiopattern_start_thread();

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
