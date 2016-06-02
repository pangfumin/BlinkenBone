/* gpio.c: the real-time process that handles BlinkenBoard multiplexing

 Copyright (c) 2016 Joerg Hoppe
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

 25-May-2016  JH	created

 The PDP-15 is connected to a single BlinkenBoard.
 The panel consists of two independed boards:
 - Indicator Board with the light bulbs,
 - Switch Board with switches.
 switches and lamps each are arranged in a matrix and accessed via multiplexing.
 There are 3 mux signals (TWO, ONE, ZERO).

 Board      MUX codes   Output
 indicator  3,5,7       OUT7, bits<7:5>     other codes: all lamps OFF
 switch     1,2,6       OUT0, bits<2:0>

 Lamp protection:
 The lamps are light bulbs with 12V rating and are driven with 30V.
 A separate ATmega88 micro controller monitors the
 indicator-MUX signal and switches 30V OFF if the MUX pattern is
 lighting a row more than 50 millisconds.

 */

#define _GPIO_C_

#include <time.h>
#include <pthread.h>
#include <stdint.h>

#include "blinkenbus.h" // from std server

#include "main.h"   // pdp15_panel
#include "gpio.h"

static blinkenbus_map_t blinkenbus_output_cache;
static blinkenbus_map_t blinkenbus_input_cache;



static void microsleep(unsigned microsecs)
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000 * microsecs;
            nanosleep(&ts, NULL);
        }



// write mux to BlinkenBoard #0, OUT7, Bits 7:5
static void write_indicator_mux_code( unsigned mux_code)
{
    unsigned char regval = (mux_code & 7) << 5;
    blinkenbus_register_write(0, 7, regval);
}

// write mux to BlinkenBoard #0, OUT0, Bits 2:0
static void write_switch_mux_code(unsigned mux_code)
{
    unsigned char regval = mux_code & 7;
    blinkenbus_register_write(0, 0, regval);
}

// set the BlinkenBoard register bits for a lamp mux row
//
// muxcode == 0: all lamps OFF by setting mux select to 0
//
// Limitation: A control value is completely assembled from the same cache
// and can not span several MUX rows.
static void outputcontrols_to_mux_row(unsigned mux_code)
{
    unsigned i_control;
    blinkenlight_control_t *c;
    unsigned c_muxcode;

    if (mux_code != 0) {

        // lamp test: show light, but controls hold their state
        for (i_control = 0; i_control < pdp15_panel->controls_count; i_control++) {
            c = &(pdp15_panel->controls[i_control]);
            // patch: all wirings of control must have same mux code
            c_muxcode = c->blinkenbus_register_wiring[0].mux_code;

            if (mux_code == c_muxcode && !c->is_input) {
                uint64_t value;
                if (pdp15_panel->mode == RPC_PARAM_VALUE_PANEL_MODE_ALLTEST
                        || (pdp15_panel->mode == RPC_PARAM_VALUE_PANEL_MODE_LAMPTEST
                                && c->type == output_lamp))
                    value = 0xffffffffffffffff; // selftest:
                else if (pdp15_panel->mode == RPC_PARAM_VALUE_PANEL_MODE_POWERLESS
                        && c->type == output_lamp)
                    value = 0; // all OFF
                else
                    value = c->value;

                blinkenbus_outputcontrol_to_cache(blinkenbus_output_cache, c, value);
                // "write as mask": all ones, if self test
            }
        }
    }

    blinkenbus_cache_to_blinkenboards_outputs(blinkenbus_output_cache, /* force_all*/1);
}

// read the BlinkenBoard register bits for a switch mux row
//
// Limitation: A control value is completely assembled from the same cache
// and can not span several MUX rows.

static void inputcontrols_from_mux_row(unsigned mux_code)
{
    unsigned i_control;
    blinkenlight_control_t *c;
    unsigned c_muxcode;

    if (mux_code == 0)
        return;

    // LOCK
    blinkenbus_cache_from_blinkenboards_inputs(blinkenbus_input_cache);
    // UNLOCK

    for (i_control = 0; i_control < pdp15_panel->controls_count; i_control++) {
        c = &(pdp15_panel->controls[i_control]);
        // patch: all wirings of control must have same mux code
        c_muxcode = c->blinkenbus_register_wiring[0].mux_code;

        if (mux_code == c_muxcode && c->is_input) {
            blinkenbus_control_from_cache(blinkenbus_input_cache, c);
        }
    }
}

/***********************************************
 * the multiplexing thread
 * 3 switche rows and 3 lamps rows are scanned in parallel
 * there are "all OFF" cycles to regulate lamp brightness.
 *
 *
 * The
 */
void mux(int * terminate)
{
#define PHASES  3 // 3 lamp phases, some idle phases
    unsigned phase = 0;
    unsigned mux_code;
    unsigned regaddr;

    blinkenbus_init();

    while (!*terminate) {
        phase = (phase + 1) % PHASES;

        // 1. set board #0 control register periodically to "outputs enabled"
        if (phase == 0) {
            blinkenbus_board_control_write(0, 0);
        }

        // 2. drive lamp row. Option: distribute "ON" phases evenly
        switch (phase) {
        case 0:
            mux_code = 3;
            break;
        case 1:
            mux_code = 5;
            break;
        case 2:
            mux_code = 7;
            break;
        default:
            mux_code = 0; // all off
        }
        // printf("%u ", mux_code) ;
        // assemble value for BlinkenBoard output registers
        // from control&wiring struct
        // muxcode = 0: all OFF
        write_indicator_mux_code(mux_code);

        outputcontrols_to_mux_row(mux_code);

        // 3. drive switch row
        switch (phase) {
        case 0:
            mux_code = 1;
            break;
        case 1:
            mux_code = 2;
            break;
        case 2:
            mux_code = 6;
            break;
        default:
            mux_code = 0; // no scan
        }

        // 3.1. assemble switch values from BlinkenBoard intput registers
        // from control&wiring struct
        // muxcode = 0: no-op
        if (mux_code)
            write_switch_mux_code(mux_code);

        // 4. thread wait 1 millisecond
        // the ATmega samples the MUX signal with 10kHz,
        // we must generated here max of that half frequency.
        microsleep(1000) ;

        if (mux_code)
            // 3.2. read delayed, after mux has settled
            inputcontrols_from_mux_row(mux_code);
    }
}

