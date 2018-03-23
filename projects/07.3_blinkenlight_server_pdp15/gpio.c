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

 20-Sep-2016  JH    Switch polling through history-based low pass
                    Lamp flicker reduction through 50Hz low pass
 04-Aug-2016  JH    Switch polling with reduced frequency, to supress contact bounce
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
#include "print.h"
#include "iopattern.h"
#include "gpio.h"

static blinkenbus_map_t blinkenbus_input_cache;

static void microsleep(unsigned microsecs)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000 * microsecs;
    nanosleep(&ts, NULL);
}

// write mux to BlinkenBoard #0, OUT7, Bits 7:5
static void write_indicator_mux_code(unsigned mux_code)
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


// read the BlinkenBoard register bits for a switch mux row
//
// Limitation: A control value is completely assembled from the same cache
// and can not span several MUX rows.

static void inputcontrols_from_mux_row(unsigned mux_code)
{
    uint64_t now_us;
    unsigned i_control;
    blinkenlight_control_t *c;
    unsigned c_muxcode;

    if (mux_code == 0)
        return;
    now_us = historybuffer_now_us();

    // todo: LOCK?
    // fill c->value_raw
    blinkenbus_cache_from_blinkenboards_inputs(blinkenbus_input_cache);

    // todo: UNLOCK?

    for (i_control = 0; i_control < pdp15_panel->controls_count; i_control++) {
        c = &(pdp15_panel->controls[i_control]);
        // patch: all wirings of control must have same mux code
        c_muxcode = c->blinkenbus_register_wiring[0].mux_code;

        if (mux_code == c_muxcode && c->is_input) {
            // all inputs: must not touch c->value, only set c->value_raw !
            blinkenbus_inputcontrol_from_cache(blinkenbus_input_cache, c, /*raw*/TRUE);
        }
    }

    if (mux_code == MUX1) {
        /* exam/deposit this/next must be constructed separateley
         * physical keyboard signals:
         * S30 "DEP THIS"       => In2.2
         * S32 "EXAMINE THIS"   => In2.1
         * S31 "DEP NEXT" & S33 "EXAMINE NEXT" is one signal, combines with EXAM/DEPOSIT THIS
         *                      => In1.0
         *
         *        keyboard                       API
         * dep_this     dep_exam_next   exam_this   exam_next
         * 0            *               0           0
         * 1            0               1           0
         * 1            1               0           1
         *
         * exam_this    dep_exam_next   dep_this    dep_next
         * 0            *               0           0
         * 1            0               1           0
         * 1            1               0           1
         */
        int keyboard_deposit_this = !(blinkenbus_input_cache[2] & 0x4); // In2.2
        int keyboard_examine_this = !(blinkenbus_input_cache[2] & 0x2); // In2.1
        int keyboard_dep_exam_next = !(blinkenbus_input_cache[1] & 0x1); // In1.0
        static blinkenbus_map_t input_cache_prev; // to detect changes
        static int dbg_eventcnt = 0;
        extern blinkenlight_control_t *switch_deposit_this; // main.c
        extern blinkenlight_control_t *switch_deposit_next;
        extern blinkenlight_control_t *switch_examine_this;
        extern blinkenlight_control_t *switch_examine_next;

        if (keyboard_deposit_this == 0) {
            switch_deposit_this->value_raw = 0;
            switch_deposit_next->value_raw = 0;
        } else if (keyboard_dep_exam_next == 0) {
            switch_deposit_this->value_raw = 1;
            switch_deposit_next->value_raw = 0;
        } else {
            switch_deposit_this->value_raw = 0;
            switch_deposit_next->value_raw = 1;
        }
        if (keyboard_examine_this == 0) {
            switch_examine_this->value_raw = 0;
            switch_examine_next->value_raw = 0;
        } else if (keyboard_dep_exam_next == 0) {
            switch_examine_this->value_raw = 1;
            switch_examine_next->value_raw = 0;
        } else {
            switch_examine_this->value_raw = 0;
            switch_examine_next->value_raw = 1;
        }
    }

    // feed bouncing switches into lowpass, assign final values for others
    for (i_control = 0; i_control < pdp15_panel->controls_count; i_control++) {
        c = &(pdp15_panel->controls[i_control]);
        c_muxcode = c->blinkenbus_register_wiring[0].mux_code;

        if (mux_code == c_muxcode && c->is_input) {
            if (c->fmax > 0)
                historybuffer_set_val(c->history, now_us, c->value_raw);
            else
                c->value = c->value_raw; // no filtering
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
void gpio_mux(int * terminate)
{
#define MUX_PHASES  3 // 3 lamp phases, some idle phases
    unsigned rounds; // globbal ticker
    unsigned mux_sleep_us;
//    unsigned phase = 0;
//    unsigned mux_code;
    unsigned regaddr;

    unsigned switch_prescaler_val;
//    unsigned switch_prescaler = 0;

// default
    if (opt_mux_frequency)
        mux_sleep_us = 1000000 / opt_mux_frequency;
    else
        mux_sleep_us = 1000; // 1 kHz
    print(LOG_NOTICE, "Lamp multiplexing period = %u us.\n", mux_sleep_us);

    /*
     switch_prescaler_val = 0;
     if (opt_switch_mux_frequency)
     switch_prescaler_val = opt_mux_frequency / opt_switch_mux_frequency;
     if (switch_prescaler_val <= 0)
     // else  poll switch with 10 Hz
     switch_prescaler_val =
     print(LOG_NOTICE, "Switch multiplexing period = %d Hz = %u us.\n", opt_switch_mux_frequency,
     switch_prescaler_val * mux_sleep_us);
     */
    switch_prescaler_val = opt_mux_frequency / 30; //  poll switches with 30 Hz
    print(LOG_NOTICE, "Switch multiplexing period = 30 Hz = %u us.\n",
            switch_prescaler_val * mux_sleep_us);

    blinkenbus_init();

// set thread to real time priority
// seems not to work under BeagleBone Angstrom: now realtime-kernel?
    {
        struct sched_param sp;
        int policy, prio;
        int res;
        sp.sched_priority = 10;
        // sp.sched_priority = 98; // maybe 99, 32, 31?
        //res = pthread_setschedparam(pthread_self(), SCHED_RR, &sp) ;
        pthread_getschedparam(pthread_self(), &policy, &sp);
        prio = sp.sched_priority + 1;
        res = pthread_setschedprio(pthread_self(), prio);
        // res = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) ;
        if (res)
            print(LOG_ERR,
                    "Warning: failed to set RT priority to %d, pthread_setschedparam() returned %d\n",
                    prio, res);
        // 1 = EPERM
        // 22 = EINVAL
    }

#ifdef TEST
    {
        extern blinkenlight_control_t *lamp_register; // I29-I46 "R00-R17"
        unsigned phase;
        unsigned c_muxcode ;
        unsigned value = 0777777;//0123456;
        blinkenlight_control_t *c = lamp_register; // I29-I46 "R00-R17"
        c_muxcode =  c->blinkenbus_register_wiring[0].mux_code; // something between 0..7

        for (phase = 0; phase < IOPATTERN_OUTPUT_PHASES; phase++) {
//            for (c_muxcode = 0; c_muxcode <= MAX_MUX_CODE; c_muxcode++) {
                blinkenbus_outputcontrol_to_cache(blinkenbus_output_caches[phase][c_muxcode], c,
                        value & (0xfffff >> (phase)));
            }
    }
#endif

    rounds = 0;
    while (!*terminate) {
        unsigned lamp_brightness_phase;
        unsigned lamp_mux_phase;
        unsigned switch_mux_phase;
        unsigned mux_code;

        rounds = (rounds + 1) & 0x7ffffff; // inc global clock
        // on roll around, output mux/poll order is disturbed ... so what?

        // 1. set board #0 control register periodically to "outputs enabled"
        if (rounds % 1000) {
            blinkenbus_board_control_write(0, 0);
        }

        // 2. drive lamp row. Option: distribute "ON" phases evenly
        // total output phases:
        // 3 for output multiplexing,
        // 16 for brightness phases => one brightness phase every 50 rounds

        lamp_brightness_phase = (rounds / MUX_PHASES) % IOPATTERN_OUTPUT_PHASES;
        lamp_mux_phase = rounds % MUX_PHASES;
        switch (lamp_mux_phase) {
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

        write_indicator_mux_code(mux_code);

        // TODO: LAMPTEST logic in iopattern.c::iopattern_update_outputs()
        blinkenbus_cache_to_blinkenboards_outputs(
                blinkenbus_output_caches[lamp_brightness_phase][mux_code], /* force_all*/1);

        // outputcontrols_to_mux_row(mux_code);

        if (rounds % switch_prescaler_val != 0) {
            microsleep(mux_sleep_us); // do not set lamp rows too fast after another
        } else {
            // poll switches with reduced frequency
            switch_mux_phase = (rounds / switch_prescaler_val) % 3;
//            print(LOG_NOTICE, " %d\n", switch_mux_phase) ;

            // poll switches for 3 phases = prescaler 2,1,0
            // 3. drive switch row
            switch (switch_mux_phase) {
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

            // 3.1. assemble switch values from BlinkenBoard input registers
            // from control&wiring struct
            // muxcode = 0: no-op
            if (mux_code)
                write_switch_mux_code(mux_code);

            // 4. thread wait 1 millisecond
            // the ATmega samples the MUX signal with 10kHz,
            // we must generated here max of that half frequency.
            microsleep(mux_sleep_us);

            if (mux_code) {
                // 3.2. read delayed, after mux has settled
                inputcontrols_from_mux_row(mux_code);
            }
        }
    }
}

