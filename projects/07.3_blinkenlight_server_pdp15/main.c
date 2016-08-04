/* main.c: Blinkenlight API server for PDP-15 on BlinkenBoard

 Copyright (c) 2016, Joerg Hoppe
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


 01-Aug-2016  JH    DEPOSIT/EXAMINE_THIS/NEXT separate (like keys, not internal combo signals)
 25-Jun-2016  JH    DATA/ADDR switches, REGISTER, MEMBUFFER: mirror, Bit17 = LSB
 25-Jun-2016  JH    commandline processing changed to getopt2
 25-Jun-2016  JH    mux frequency settable in command line
 18-May-2016  JH    V 1.0 created


 Blinkenlight API server, which controls lamps and switches of the PDP-15
 panel connected to BlinkenBus/BlinkenBoard.
 The generic configurable "07.1 blinkenlightd" server can not be used here, because
 the PDP-15 panel accesses lamps and switches in a multiplexed manner.


 Timing & CPU load:
 There is one  threads:
 a) the LED & switch MUX loop, in gpio.c, long intervl

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
#include "getopt2.h"

#include "main.h"
#include "gpio.h"

// all data structs work on this panel
volatile blinkenlight_panel_t *pdp15_panel;

// "blinkenlight_panel_list" from blinkenlight_api_server_procs

char program_info[1024];
char program_name[1024]; // argv[0]
char program_options[1024]; // argv[1.argc-1]
int opt_test = 0;
int opt_background = 0;
unsigned opt_mux_frequency = 0;

// command line args
static getopt_t getopt_parser;

/*
 *	PDP-15 controls wich are accessible over Blinkenlight API
 */

// *** Indicator Board ***
// Comments: schematic indicator component number & name
blinkenlight_control_t *lamp_dch_active; // I1  "DCH ACTIVE"
blinkenlight_control_t *lamps_api_states_active; // I2-I6,I8-I10 "PL00-07"
blinkenlight_control_t *lamp_api_enable; // I11 "API ENABLE"
blinkenlight_control_t *lamp_pi_active; // I12 "PI ACTIVE"
blinkenlight_control_t *lamp_pi_enable; // I13 "PI ENABLE"
blinkenlight_control_t *lamp_mode_index; // I14 "INDEX MODE"
blinkenlight_control_t *lamp_major_state_fetch; // I15 "FETCH"
blinkenlight_control_t *lamp_major_state_inc; // I16 "INC"
blinkenlight_control_t *lamp_major_state_defer; // I17 "DEFER"
blinkenlight_control_t *lamp_major_state_eae; // I18 "EAE"
blinkenlight_control_t *lamp_major_state_exec; // I19 "EXECUTE"
blinkenlight_control_t *lamps_time_states; // I20-I22 "TS1-TS3"

blinkenlight_control_t *lamp_extd; // I25 "EXTD"
blinkenlight_control_t *lamp_clock; // I26 "CLOCK"
blinkenlight_control_t *lamp_error; // I27 "ERROR"
blinkenlight_control_t *lamp_prot; // I28 "PRTCT"  "protect"
blinkenlight_control_t *lamp_link; // I7 "LINK"
blinkenlight_control_t *lamp_register; // I29-I46 "R00-R17"

blinkenlight_control_t *lamp_power; // I24 "POWER"
blinkenlight_control_t *lamp_run; // I23 "RUN"
blinkenlight_control_t *lamps_instruction; // I47-I50 "IR00-IR03"
blinkenlight_control_t *lamp_instruction_defer; // I51 "OP DEFER"
blinkenlight_control_t *lamp_instruction_index; // I52 "OP INDEX"
blinkenlight_control_t *lamp_memory_buffer; // I53-I70 "MB00..17"

// *** Switch Board ***
blinkenlight_control_t *switch_stop; // S1 "STOP"
blinkenlight_control_t *switch_reset; // S2 "RESET"
blinkenlight_control_t *switch_read_in; // S3 "READ IN"
blinkenlight_control_t *switch_reg_group; // S4 "REG GROUP"
blinkenlight_control_t *switch_clock; // S5 "CLK"
blinkenlight_control_t *switch_bank_mode; // S6 "BANK MODE"
blinkenlight_control_t *switch_rept; // S7 "REPT"
blinkenlight_control_t *switch_prot; // S8 "PROT"
blinkenlight_control_t *switch_sing_time; // S9 "SING TIME"
blinkenlight_control_t *switch_sing_step; // S10 "SING STEP"
blinkenlight_control_t *switch_sing_inst; // S11 "SING INST"
blinkenlight_control_t *switch_address; // S12-S26  "ADSW03-17"

blinkenlight_control_t *switch_start; // S27 "START"
blinkenlight_control_t *switch_exec; // S28 "EXECUTE"
blinkenlight_control_t *switch_cont; // S29 "CONT"
blinkenlight_control_t *switch_deposit_this; // constructed
blinkenlight_control_t *switch_examine_this; // constructed
blinkenlight_control_t *switch_deposit_next; // constructed
blinkenlight_control_t *switch_examine_next; // constructed
blinkenlight_control_t *switch_data; // S34-S51 "DSW0-17"

blinkenlight_control_t *switch_power; // S53
blinkenlight_control_t *potentiometer_repeat_rate;

blinkenlight_control_t *switch_register_select; // S52

/*
 *	RPC server callbacks:
 */
static void on_blinkenlight_api_panel_get_controlvalues(blinkenlight_panel_t *p)
{
    // gets called when RPC client wants panel input control values
    unsigned i;
    for (i = 0; i < p->controls_count; i++) {
        blinkenlight_control_t *c = &p->controls[i];
        if (c->is_input) {
            if (c == switch_power)
                c->value = 1; // send "power" switch as ON
        }
    }
}

static void on_blinkenlight_api_panel_set_controlvalues(blinkenlight_panel_t *p, int force_all)
{
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
void *mux(void *ptr); // the real-time GPIO multiplexing process to start up

pthread_t mux_thread;
int mux_thread_terminate = 0;

static void gpio_mux_thread_start()
{
    int res;
    res = pthread_create(&mux_thread, NULL, mux, &mux_thread_terminate);
    if (res) {
        fprintf(stderr, "Error creating gpio_mux thread, return code %d\n", res);
        exit(EXIT_FAILURE);
    }
    printf("Created \"gpio_mux\" thread\n");

    sleep(2); // allow 2 sec for multiplex to start
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
    print(LOG_NOTICE,
            "*** pdp15_blinkenlightd %s - server for BlinkenBone PDP-15 console panel ***\n",
            VERSION);
    print(LOG_NOTICE, "    Compiled " __DATE__ " " __TIME__ "\n");
    print(LOG_NOTICE, "    Copyright (C) 2016 Joerg Hoppe.\n");
    print(LOG_NOTICE, "    Contact: j_hoppe@t-online.de, www.retrocmp.com\n");
    print(LOG_NOTICE, "\n");
}

/*
 * help()
 */
static void help()
{
    fprintf(stderr, "\n");
    fprintf(stderr,
            "pdp15_blinkenlightd %s - Blinkenlight RPC server for BlinkenBone PDP-15 console panel \n",
            VERSION);
    fprintf(stderr, "  (compiled " __DATE__ " " __TIME__ ")\n");
    // getop must be intialized to print the syntax
    getopt_help(&getopt_parser, stdout, 80, 10, "blinkenlightd");
    exit(1);
}

// show error for one option
static void commandline_error()
{
    fprintf(stdout, "Error while parsing commandline:\n");
    fprintf(stdout, "  %s\n", getopt_parser.curerrortext);
    exit(1);
}
// parameter wrong for currently parsed option
static void commandline_option_error()
{
    fprintf(stdout, "Error while parsing commandline option:\n");
    fprintf(stdout, "  %s\nSyntax:  ", getopt_parser.curerrortext);
    getopt_help_option(&getopt_parser, stdout, 80, 10);
    exit(1);
}

/*
 * read commandline parameters into global vars
 * result: 0 = OK, 1 = error
 */
static void parse_commandline(int argc, char **argv)
{
    int res;

    // define commandline syntax
    getopt_init(&getopt_parser, /*ignore_case*/1);
    getopt_def(&getopt_parser, "?", "help", NULL, NULL, NULL, "Print help", NULL, NULL, NULL, NULL);

    getopt_def(&getopt_parser, "b", "background", NULL, NULL, NULL,
            "background operation: print to syslog (view with dmesg)\n"
                    "Else default output is stderr",
            NULL, NULL, NULL, NULL);
    getopt_def(&getopt_parser, "v", "verbose", NULL, NULL, NULL, "verbose: tell what I'm doing",
    NULL, NULL, NULL, NULL);
    getopt_def(&getopt_parser, "t", "test", NULL, NULL, NULL, "Test mode",
    NULL, NULL, NULL, NULL);

    getopt_def(&getopt_parser, "mf", "muxfrequency", "frequency", NULL, "1000",
            "Row frequency for lamp multiplexing. There are 3 rows.\n"
                    "!! For values != 1000 (1 kHz) the lamp protection watchdog will not work correctly !!",
            "10000", "multiplex with 10 kHz, every row is shown 3333x per second", NULL, NULL);

    res = getopt_first(&getopt_parser, argc, argv);
    while (res > 0) {
        if (getopt_isoption(&getopt_parser, "help")) {
            help();
        } else if (getopt_isoption(&getopt_parser, "background")) {
            opt_background = 1;
        } else if (getopt_isoption(&getopt_parser, "test")) {
            opt_test = 1;
        } else if (getopt_isoption(&getopt_parser, "verbose")) {
            print_level = LOG_DEBUG;
        } else if (getopt_isoption(&getopt_parser, "muxfrequency")) {
            if (getopt_arg_i(&getopt_parser, "frequency", &opt_mux_frequency) < 0)
                commandline_option_error();

        }
        res = getopt_next(&getopt_parser);

    }
    // start logging
    print_open(opt_background); // if background, then syslog

    if (res == GETOPT_STATUS_MINARGCOUNT || res == GETOPT_STATUS_MAXARGCOUNT)
        // known option, but wrong number of arguments
        commandline_option_error();
    else if (res < 0)
        commandline_error();
}

/*
 * Define part of the fix controls of the PDP15
 * for the Blinkenlight API interface structs
 * - control_value_bit_offset: position of bitfield in final control value
 * - bitlen: count of bits in status register to be mounted into value
 * - mux_code: active if mux code at this value
 * - reg_addr: register address on BlinkenBoard #0
 * - bit_offset: LSB of bitfield, to be shifted this much
 *
 * See http://retrocmp.com/projects/blinkenbone/blinkenbone-physical-panels/173-blinkenbone-blinkenlightd-patch-field-decoding-and-console-panel-simulator
 */
static blinkenlight_control_t *define_switch_slice(blinkenlight_panel_t *p, char *name,
        unsigned control_value_bit_offset, unsigned bitlen, unsigned mux_code, unsigned reg_addr,
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
    bbrw->mux_code = mux_code;
    bbrw->board_register_space = input_register;
    bbrw->board_register_address = reg_addr;
    bbrw->control_value_bit_offset = control_value_bit_offset;
    bbrw->blinkenbus_lsb = bit_offset;
    bbrw->blinkenbus_msb = bbrw->blinkenbus_lsb + bitlen - 1;
    // all switches inverted: bit 1 => switch up in "0" position
    bbrw->blinkenbus_levels_active_low = 1;

    return c;
}

// define outputs, see register_switch_slice()
static blinkenlight_control_t *define_lamp_slice(blinkenlight_panel_t *p, char *name,
        unsigned control_value_bit_offset, unsigned bitlen, unsigned mux_code, unsigned reg_addr,
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
    bbrw->mux_code = mux_code;
    bbrw->board_register_space = output_register;
    bbrw->board_register_address = reg_addr;
    bbrw->control_value_bit_offset = control_value_bit_offset;
    bbrw->blinkenbus_lsb = bit_offset;
    bbrw->blinkenbus_msb = bbrw->blinkenbus_lsb + bitlen - 1;
    bbrw->blinkenbus_levels_active_low = 1; // all lampdrivers inverting

    return c;
}

static void register_controls()
{
    blinkenlight_panel_t *p;

    // one global panel list ...
    blinkenlight_panel_list = blinkenlight_panels_constructor();
    // ... with one panel
    p = blinkenlight_add_panel(blinkenlight_panel_list);
    pdp15_panel = p;
    strcpy(p->name, "PDP15");
    strcpy(p->info, "PDP-15 console panel");

    /*
     * Construct high-level Blinkenlight API controls from
     * hardware switch- and led-registers
     */
    // *** Indicator Board ***
    // params: value offset, bitlen, muxcode, OUT register, register bit offset
    // see panels/pdp15/schematic.txt
    lamp_dch_active = define_lamp_slice(p, "DCH_ACTIVE", 0, 1, MUX7, 8, 0); // Out 8.0
    lamps_api_states_active = define_lamp_slice(p, "API_STATES_ACTIVE", 0, 5, MUX7, 8, 1); // Out8.1:5 "PL00-04"
    lamps_api_states_active = define_lamp_slice(p, "API_STATES_ACTIVE", 5, 3, MUX7, 9, 1); // Out9.1:3 "PL05-07"
    lamp_api_enable = define_lamp_slice(p, "API_ENABLE", 0, 1, MUX7, 9, 4); // Out9.4
    lamp_pi_active = define_lamp_slice(p, "PI_ACTIVE", 0, 1, MUX7, 9, 5); // Out9.5
    lamp_pi_enable = define_lamp_slice(p, "PI_ENABLE", 0, 1, MUX7, 9, 6); // Out9.6
    lamp_mode_index = define_lamp_slice(p, "MODE_INDEX", 0, 1, MUX7, 10, 0); // Out10.0
    lamp_major_state_fetch = define_lamp_slice(p, "STATE_FETCH", 0, 1, MUX7, 6, 0); // Out6.0
    lamp_major_state_inc = define_lamp_slice(p, "STATE_INC", 0, 1, MUX7, 6, 1); // Out6.1
    lamp_major_state_defer = define_lamp_slice(p, "STATE_DEFER", 0, 1, MUX7, 6, 2); // Out6.2
    lamp_major_state_eae = define_lamp_slice(p, "STATE_EAE", 0, 1, MUX7, 6, 3); // Out6.3
    lamp_major_state_exec = define_lamp_slice(p, "STATE_EXEC", 0, 1, MUX7, 6, 4); // Out6.4
    lamps_time_states = define_lamp_slice(p, "TIME_STATES", 0, 3, MUX7, 6, 5); // Out6.5:7 "TS1-TS3"

    lamp_extd = define_lamp_slice(p, "EXTD", 0, 1, MUX3, 8, 2); // Out 8.2
    lamp_clock = define_lamp_slice(p, "CLOCK", 0, 1, MUX3, 8, 3); // Out 8.3
    lamp_error = define_lamp_slice(p, "ERROR", 0, 1, MUX3, 8, 4); // Out 8.4
    lamp_prot = define_lamp_slice(p, "PROT", 0, 1, MUX3, 8, 5); // Out 8.5 "PRTCT"  "protect"
    lamp_link = define_lamp_slice(p, "LINK", 0, 1, MUX7, 9, 0); // Out 9.0, MUX 7
    lamp_register = define_lamp_slice(p, "REGISTER", 0, 8, MUX3, 9, 0); // Out 9.0:7 "R00-R07"
    lamp_register = define_lamp_slice(p, "REGISTER", 8, 2, MUX3, 10, 0); // Out 10.0:1 "R08-R09"
    lamp_register = define_lamp_slice(p, "REGISTER", 10, 8, MUX3, 6, 0); // Out 6.0:7 "R10-R17"
    lamp_register->mirrored_bit_order = 1; // bit 0 is MSB
    lamp_power = define_lamp_slice(p, "POWER", 0, 1, MUX3, 8, 1); // Out 8.1
    lamp_run = define_lamp_slice(p, "RUN", 0, 1, MUX3, 8, 0); // Out 8.0

    lamps_instruction = define_lamp_slice(p, "INSTRUCTION", 0, 4, MUX5, 8, 0); // Out 8.0:3 "IR00-IR03"
    lamps_instruction->mirrored_bit_order = 1; // bit 0 is MSB
    lamp_instruction_defer = define_lamp_slice(p, "INSTRUCTION_DEFER", 0, 1, MUX5, 8, 4); // Out 8.4 "OP DEFER"
    lamp_instruction_index = define_lamp_slice(p, "INSTRUCTION_INDEX", 0, 1, MUX5, 8, 5); // Out 8.5 "OP INDEX"
    lamp_memory_buffer = define_lamp_slice(p, "MEMORY_BUFFER", 0, 8, MUX5, 9, 0); // Out 9.0:7 "MB00-MB07"
    lamp_memory_buffer = define_lamp_slice(p, "MEMORY_BUFFER", 8, 2, MUX5, 10, 0); // Out 10.0:1 "MB08-MB09"
    lamp_memory_buffer = define_lamp_slice(p, "MEMORY_BUFFER", 10, 8, MUX5, 6, 0); // Out 6.0:7 "MB10-MB17"
    lamp_memory_buffer->mirrored_bit_order = 1; // bit 0 is MSB

    // *** Switch Board ***
    // params: value offset, bitlen, muxcode, OUT register, register bit offset
    // see panels/pdp15/schematic.txt

    switch_stop = define_switch_slice(p, "STOP", 0, 1, MUX1, 1, 1); // In1.1 "STOP"
    switch_reset = define_switch_slice(p, "RESET", 0, 1, MUX1, 2, 0); // In2.0 "RESET"
    switch_read_in = define_switch_slice(p, "READ_IN", 0, 1, MUX1, 1, 2); // In1.2 "READ IN"
    switch_reg_group = define_switch_slice(p, "REG_GROUP", 0, 1, MUX6, 1, 0); // In 1.0 "REG GROUP"
    switch_clock = define_switch_slice(p, "CLOCK", 0, 1, MUX6, 1, 1); // In1.1 "CLK"
    switch_bank_mode = define_switch_slice(p, "BANK_MODE", 0, 1, MUX6, 1, 2); // In1.2 "BANK MODE"
    switch_rept = define_switch_slice(p, "REPT", 0, 1, MUX6, 1, 3); // In1.3 "REPT"
    switch_prot = define_switch_slice(p, "PROT", 0, 1, MUX6, 1, 4); // In1.4 "PROT"
    switch_sing_time = define_switch_slice(p, "SING_TIME", 0, 1, MUX6, 1, 5); // In 1.5 "SING TIME"
    switch_sing_step = define_switch_slice(p, "SING_STEP", 0, 1, MUX6, 2, 0); // In2.0 "SING STEP"
    switch_sing_inst = define_switch_slice(p, "SING_INST", 0, 1, MUX6, 2, 1); // In2.1 "SING INST"
    switch_address = define_switch_slice(p, "ADDRESS", 0, 5, MUX6, 2, 3); // In2.3:7 "ADSW03-07" -> 0:4
    switch_address = define_switch_slice(p, "ADDRESS", 5, 2, MUX6, 3, 0); // In3.0:1 "ADSW08-09"  -> 5:6
    switch_address = define_switch_slice(p, "ADDRESS", 7, 8, MUX6, 0, 0); // In0.0:7 "ADSW10-17" ->7:14
    switch_address->mirrored_bit_order = 1; // bit 0 is MSB

    switch_start = define_switch_slice(p, "START", 0, 1, MUX1, 2, 3); // In2.3 "START"
    switch_exec = define_switch_slice(p, "EXECUTE", 0, 1, MUX1, 1, 3); // In1.3 "EXECUTE"
    switch_cont = define_switch_slice(p, "CONT", 0, 1, MUX1, 1, 5); // In1.5 "CONT"
    // EXAM/DEPOSIT THIS/NEXT are constructed in gpio.c;
    switch_deposit_this = define_switch_slice(p, "DEPOSIT_THIS", 0, 1, MUX1, 0, 0); // register&bit dummy
    switch_examine_this = define_switch_slice(p, "EXAMINE_THIS", 0, 1, MUX1, 0, 0); // "
    switch_deposit_next = define_switch_slice(p, "DEPOSIT_NEXT", 0, 1, MUX1, 0, 0); // "
    switch_examine_next = define_switch_slice(p, "EXAMINE_NEXT", 0, 1, MUX1, 0, 0); // "

    switch_data = define_switch_slice(p, "DATA", 0, 8, MUX2, 2, 0); // In2.0:7 "DSW00-07"
    switch_data = define_switch_slice(p, "DATA", 8, 2, MUX2, 3, 0); // In3.0:1 "DSW08-09"
    switch_data = define_switch_slice(p, "DATA", 10, 8, MUX2, 0, 0); // In0.0:7 "DSW10-17"
    switch_data->mirrored_bit_order = 1; // bit 0 is MSB

    // coded as moving bit
    switch_register_select = define_switch_slice(p, "REGISTER_SELECT", 0, 1, MUX1, 2, 5); // In2.5 "REGSEL.1"
    switch_register_select = define_switch_slice(p, "REGISTER_SELECT", 1, 1, MUX1, 2, 7); // In2.7 "REGSEL.2"
    switch_register_select = define_switch_slice(p, "REGISTER_SELECT", 2, 2, MUX1, 3, 0); // In3.0 "REGSEL.3:4"
    switch_register_select = define_switch_slice(p, "REGISTER_SELECT", 4, 8, MUX1, 0, 0); // In0.0 "REGSEL.5:12"

    switch_power = define_switch_slice(p, "POWER", 0, 1, 0, 0, 0); // dummy, always 1
    // Constant values set in on_blinkenlight_api_panel_get_controlvalues()

    // the potentiometer is tied to IN4, non-muxed = all MUX codes
    potentiometer_repeat_rate = define_switch_slice(p, "REPEAT_RATE", 0, 8, MUX1, 4, 0); // In4.0:7

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
    parse_commandline(argc, argv);

    sprintf(program_info, "PDP-15 blinkenlightd - Blinkenlight API server daemon for PDP-15 %s",
    VERSION);

    info();

    print(LOG_INFO, "Start\n");
#ifdef TEST
    while(1) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000 * 100000/*microsecs*/;
        nanosleep(&ts, NULL);
    }
    exit(0);
#endif

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
    // does never end!

    blinkenlight_api_server();
    // does never end!

    print_close(); // well ....

    blinkenlight_panels_destructor(blinkenlight_panel_list);

    return 0;
}
