/* iopattern.c: transform Blinkenlight API values into muxed BlinkenBus analog levels

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

 17-Sep-2016  JH      created


 2 threads are implemented:
 - lowpass thread in iopattern.c : updates the analog values to display at quite low freqency:
 20 to 50 Hz. For 16 brightness levels, 16 SimH messages must be sampled.
 So if REALCONS is running at 1ms intervals, low pass interval must be at least 16ms.

 - muxthread in gpio.c
 This drives the blinkenbus multiplexing in high speed.
 A "cache" contains the should-be state of all BlinkenBus registers and is
 transfered to the driver in optimized way.
 For 16 brightness levels there are 16 mux phases,
 each contains one on/off state of all controls.
 */

#define IOPATTERN_C_

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

#include "print.h"

#include "blinkenbus.h"
#include "iopattern.h"
#include "gpio.h"

#include "main.h"   // global blinkenlight_panel_list

// one cache for outputs
// per display phase
// per lamp mux row. 3 rows neede, codes 0..7 -> only 3 of 8 buffers needed.
//      waste space to get speed
blinkenbus_map_t blinkenbus_output_caches[IOPATTERN_OUTPUT_PHASES][MAX_MUX_CODE + 1];

/*
 * low frequency thread sets brightnesses
 */
pthread_t iopattern_update_thread;
int iopattern_update_thread_terminate = 0;

#if 1
/*
 * Table for bitvalues for different display phases and a given brigthness level.
 * Generated with "LedPatterns.exe"
 * The relation between perceived brightness and required pattern is normally non-linear.
 * This table depends on the driver electronic and needs to be fine-tuned.
 * Brightness-to-duty-cycle function: "Linear"
 * Pattern style: "2xPWM".
 */
//#define GPIOPATTERN_LED_BRIGHTNESS_LEVELS 16 // brightness levels
//#define GPIOPATTERN_LED_BRIGHTNESS_PHASES 15 // normally, <n> brightness levels need <n>-1 phases
char brightness_phase_lookup[16][15] =
{ //
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //  0/15 =  0%
                { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //  1/15 =  7%
                { 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, //  2/15 = 13%
                { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 }, //  3/15 = 20%
                { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 }, //  4/15 = 27%
                { 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 }, //  5/15 = 33%
                { 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0 }, //  6/15 = 40%
                { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 }, //  7/15 = 47%
                { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0 }, //  8/15 = 53%
                { 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0 }, //  9/15 = 60%
                { 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0 }, // 10/15 = 67%
                { 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0 }, // 11/15 = 73%
                { 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0 }, // 12/15 = 80%
                { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0 }, // 13/15 = 87%
                { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 }, // 14/15 = 93%
                { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 } // 15/15 = 100%
        };
#endif

/*
 * A PWM pattern is used, because it drastically reduces switche events write cycles
 * on the BlinkenBus.
 * MUX Frequency must be 2x perception frequency = 60Hz. Is only 21Hz (1kHz/3/16!
 */

/*
 * - averages the Blinkenlight API outputs,
 * - generates the LED brightness patterns
 * - swaps the double buffer
 *
 * This here should run in a low-frequency thread.
 * recommended frequency: 50 Hz, much lower than SimH update rate
 *
 */

// override value with selftest/"power off" values
static uint64_t panel_mode_control_value(blinkenlight_control_t *c, uint64_t value)
{
    unsigned panel_mode = c->panel->mode;

    if (panel_mode == RPC_PARAM_VALUE_PANEL_MODE_ALLTEST
            || (panel_mode == RPC_PARAM_VALUE_PANEL_MODE_LAMPTEST && c->type == output_lamp))
        return 0xffffffffffffffff; // selftest:
    if (panel_mode == RPC_PARAM_VALUE_PANEL_MODE_POWERLESS && c->type == output_lamp)
        return 0; // all OFF

    return value; // default: unchanged
}

/****************************************
 * This thread calculates the blinkenbus_output_caches[phase][muxrow]
 * from averaged control values.
 * phases for bightness: run from 0..31
 * mux = muxrow of output lamp array= 0..2
 */
void *iopattern_update_outputs(void *arg)
{
    blinkenlight_panel_t *p = pdp15_panel; // alias to global panel
    int *terminate = (int *) arg;
    uint64_t now_us;
    unsigned i_panel;
    unsigned i_control;

    do { // at least one cycle, for non-thread call
        struct timespec wait;

        wait.tv_sec = 0;
        wait.tv_nsec = IOPATTERN_UPDATE_PERIOD_US * 1000;
        // wait for one period
        nanosleep(&wait, NULL);

        now_us = historybuffer_now_us();

        // output mount bits for all panels, all controls and all phases
        // into phase cache page

        for (i_control = 0; i_control < p->controls_count; i_control++) {
            blinkenlight_control_t *c = &p->controls[i_control];
            unsigned c_muxcode = c->blinkenbus_register_wiring[0].mux_code; // something between 0..7

            unsigned bitidx;
            unsigned phase;
            if (c->is_input)
                continue;

            // fetch  shift
            // get averaged values
            if (c->fmax == 0) {
                uint64_t value = panel_mode_control_value(c, c->value);
                // no averaging: brightness only 0 or 1, all phases identical
                for (phase = 0; phase < IOPATTERN_OUTPUT_PHASES; phase++) {
                    blinkenbus_outputcontrol_to_cache(blinkenbus_output_caches[phase][c_muxcode], c,
                            value);
                }
            } else {
                // average each bit
                historybuffer_get_average_vals(c->history, 1000000 / c->fmax, now_us, /*bitmode*/
                1);

                /*
                 // construct un-averaged value
                 for (bitidx = 0; bitidx < c->value_bitlen; bitidx++)
                 c->averaged_value_bits[bitidx] = (bitidx + 1) * 8 * ((c->value >> bitidx) & 1);
                 //MSB s brighter
                 */

                // build the display value from the low-passed bits.
                // for all display phases :
                for (phase = 0; phase < IOPATTERN_OUTPUT_PHASES; phase++) {
                    unsigned value = 0;
                    // for all bits :
                    for (bitidx = 0; bitidx < c->value_bitlen; bitidx++) {
                        // mount phase bit
                        unsigned bit_brightness = ((unsigned) (c->averaged_value_bits[bitidx])
                                * (IOPATTERN_OUTPUT_BRIGHTNESS_LEVELS)) / 256; // from 0.. 255 to
                        // fixup 0/1 flicker: very low brightness rounded to 0?
                        if (bit_brightness == 0 && c->averaged_value_bits[bitidx] > 0)
                            bit_brightness = 1;
                        assert(bit_brightness < IOPATTERN_OUTPUT_BRIGHTNESS_LEVELS);
                        if (brightness_phase_lookup[bit_brightness][phase])
                            value |= 1 << bitidx;
                    }
                    // output to cache phase

                    // override low passed pattern by lamp test
                    value = panel_mode_control_value(c, value);

                    blinkenbus_outputcontrol_to_cache(blinkenbus_output_caches[phase][c_muxcode], c,
                            value);
                }
            }
        }
    } while (*terminate == 0);
    return 0;
}
