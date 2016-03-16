/* gpiopattern.c: pattern generator, transforms data bewtwwen Blinkenlight APi and gpio-MUX

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


 14-Mar-2016  JH      created


 Oscars's gpio.c module fetches LED data from gpio_ledstatus[] array
 and drives the display MUX. This is enhanced to display LEDs in different brightness levels.
 Brightness levels are delivered by network data fed through the software-low pass.

 Time pattern
 ============

 To dim the led, a bit in gpio_ledstatus[] is replaced by a temporal on/off pattern.
 there are 16 dim levels:
 dim level       -- temporal pattern: 15 phases -->
 0               0 0 0 0 0 0 0 0 0 0 0 0 0 0 0   OFF
 1               1 0 0 0 0 0 0 0 0 0 0 0 0 0 0   ON 1/16 of time
 2               1 0 0 0 0 0 0 0 1 0 0 0 0 0 0   ON 2/16 of time
 3               1 0 0 0 0 1 0 0 0 0 1 0 0 0 0   ON 3/16 of time
 4               1 0 0 0 1 0 0 0 1 0 0 0 1 0 0   ON 4/16 of time
 ....
 14              1 1 1 1 1 1 1 1 1 1 1 1 1 1 0   ON 15/16 of time
 15              1 1 1 1 1 1 1 1 1 1 1 1 1 1 1   ON


 So gpio_ledstatus[idx] is replaced by gpiopattern_ledstatus_phases[phase][idx]
 with 'phase' running from 0 to 15.
 With incrementing 'phase' a bit in gpio_ledstatus_phases[phase][idx] delivers the above pattern.

 The circular phase increment is one in te mainloop of of the gpio thread

 gpio_ledstatus =  gpiopattern_ledstatus_phases[phase] ; // set alias
 phase = (phase +1) % 16 ;
 ... now code can use  'gpio_ledstatus[]' as before

 Double buffering
 =================
 The phase patterns are calculated by process uncorrelated to the timing of the MUX thread
 (either Blinkenlight API transmission or independent thread)
 To prevent visual resonance effects, a double buffer for gpio_ledstatus_phases[][] is used:
 one buffer is written by main(), the other buffer is read by gpio.c
 So in fact we have
 gpiopattern_ledstatus_phases[bufferidx][phase][idx]
 with bufferidx = 0 or 1

 The Global variables 'gpiopattern_bufferidx_read' and 'gpiobufferidx_write' select the buffers.
 If main() has updated the buffers, it swaps the buffers:
 gpiopattern_ledstatus_phases_readidx = gpiopattern_ledstatus_phases_writeidx ;
 gpiopattern_ledstatus_phases_writeidx= ! gpiopattern_ledstatus_phases_writeidx ;


 The code in gpio-thread now:

 ...
 gpio_ledstatus =  gpiopattern_ledstatus_phases[gpiobufferidx_read][phase] ; // set alias
 phase = (phase +1) % 16 ;
 ...



 The whole logic resides in a new module gpio_patterns.c

 */
#define GPIOPATTERN_C_

#include <time.h>
#include <assert.h>
#include "bitcalc.h"
#include "rpc_blinkenlight_api.h"
#include "gpiopattern.h"

// pointer into double buffer
int gpiopattern_ledstatus_phases_readidx = 0; // read page, used by GPIO mux
int gpiopattern_ledstatus_phases_writeidx = 1; // writepage page, written from Blinkenlight API

// pointer to BlinkenLight API panel
// context for thread.
// thread functional if != nULL
blinkenlight_panel_t *gpiopattern_blinkenlight_panel = NULL;

/*
 * old definitions
 */

// remains here for symmetry cause
volatile uint32_t gpio_switchstatus[3] =
{ 0 }; // bitfields: 3 rows of up to 12 switches
// volatile uint32_t gpio_ledstatus[8] = { 0 }; // bitfields: 8 ledrows of up to 12 LEDs

/*
 * gpiopattern_ledstatus_phases [doublebufferidx][brightness_phases] of gpio_ledstatus [gpiobank]
 * 1st index: 2 buffers
 * 2nd index: 16 brightness levels, and this much pattern phases
 * 3rd index: original Oscars's gpio bank
 *
 */
volatile uint32_t gpiopattern_ledstatus_phases[2][GPIOPATTERN_LED_BRIGHTNESS_PHASES][8];

#if 0
/*
 * Table for bitvalues for different display phases and a given brigthness level.
 * Generate with "LedPatterns.exe"
 * The relation between perceived brightness and required pattern is normally non-linear.
 * This table depends on the driver electronic and needs to be fine-tuned.
 * Pattern style: "Distributed"
 */
static char brightness_phase_lookup_32[32][31] =
{ //
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0 }, //  0/31 =  0%
				{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
						0, 0, 0, 0 }, //  1/31 =  3%
				{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
						0, 0, 0, 0 }, //  2/31 =  6%
				{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
						0, 0, 0, 0 }, //  3/31 = 10%
				{ 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
						0, 0, 0, 0 }, //  4/31 = 13%
				{ 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
						0, 0, 0, 0 }, //  5/31 = 16%
				{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
						0, 0, 0, 0 }, //  6/31 = 19%
				{ 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
						1, 0, 0, 0 }, //  7/31 = 23%
				{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,
						1, 0, 0, 0 }, //  8/31 = 26%
				{ 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0,
						0, 1, 0, 0 }, //  9/31 = 29%
				{ 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0,
						0, 1, 0, 0 }, // 10/31 = 32%
				{ 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0,
						0, 1, 0, 0 }, // 11/31 = 35%
				{ 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1,
						0, 1, 0, 0 }, // 12/31 = 39%
				{ 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1,
						0, 0, 1, 0 }, // 13/31 = 42%
				{ 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0,
						1, 0, 1, 0 }, // 14/31 = 45%
				{ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
						1, 0, 1, 0 }, // 15/31 = 48%
				{ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
						1, 0, 1, 0 }, // 16/31 = 52%
				{ 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
						1, 0, 1, 0 }, // 17/31 = 55%
				{ 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1,
						0, 1, 1, 0 }, // 18/31 = 58%
				{ 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1,
						0, 1, 1, 0 }, // 19/31 = 61%
				{ 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1,
						0, 1, 1, 0 }, // 20/31 = 65%
				{ 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0,
						1, 1, 0, 1 }, // 21/31 = 68%
				{ 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0,
						1, 1, 0, 1 }, // 22/31 = 71%
				{ 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1,
						1, 1, 0, 1 }, // 23/31 = 74%
				{ 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1,
						1, 1, 0, 1 }, // 24/31 = 77%
				{ 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1,
						1, 0, 1, 1 }, // 25/31 = 81%
				{ 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1,
						1, 0, 1, 1 }, // 26/31 = 84%
				{ 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
						0, 1, 1, 1 }, // 27/31 = 87%
				{ 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
						1, 1, 1, 1 }, // 28/31 = 90%
				{ 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
						1, 1, 1, 1 }, // 29/31 = 94%
				{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1 }, // 30/31 = 97%
				{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1 } // 31/31 = 100%
		};
#endif

#if 1
/*
 * Table for bitvalues for different display phases and a given brigthness level.
 * Generated with "LedPatterns.exe"
 * The relation between perceived brightness and required pattern is normally non-linear.
 * This table depends on the driver electronic and needs to be fine-tuned.
 * Brightness-to-duty-cycle function: "Logarithmic"
 *      logarithmic shift: 1.
 * Pattern style: "Distributed".
 */
//#define GPIOPATTERN_LED_BRIGHTNESS_LEVELS 32 // brightness levels
//#define GPIOPATTERN_LED_BRIGHTNESS_PHASES 31 // normally, n brigthness levels need -1 phases
char brightness_phase_lookup[32][31] =
{ //
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  0/31 =  0%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  1/31 =  3%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  1/31 =  3%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  1/31 =  3%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  1/31 =  3%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  2/31 =  6%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  2/31 =  6%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  2/31 =  6%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  2/31 =  6%
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, //  2/31 =  6%
  { 1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0 }, //  3/31 = 10%
  { 1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0 }, //  3/31 = 10%
  { 1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0 }, //  3/31 = 10%
  { 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0 }, //  4/31 = 13%
  { 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0 }, //  4/31 = 13%
  { 1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0 }, //  5/31 = 16%
  { 1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0 }, //  5/31 = 16%
  { 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0 }, //  6/31 = 19%
  { 1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0 }, //  7/31 = 23%
  { 1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0 }, //  7/31 = 23%
  { 1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0 }, //  8/31 = 26%
  { 1,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0,1,0,0,0,1,0,0 }, //  9/31 = 29%
  { 1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0 }, // 10/31 = 32%
  { 1,0,0,1,0,0,1,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,1,0,0,1,0,0 }, // 11/31 = 35%
  { 1,0,1,0,0,1,0,1,0,0,1,0,1,0,1,0,0,1,0,1,0,1,0,0,1,0,1,0,0,1,0 }, // 13/31 = 42%
  { 1,0,1,0,1,0,0,1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,1,0,1,0,0,1,0,1,0 }, // 14/31 = 45%
  { 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0,1,0,1,0,1,0 }, // 16/31 = 52%
  { 1,0,1,1,0,1,0,1,0,1,1,0,1,0,1,0,1,1,0,1,0,1,1,0,1,0,1,0,1,1,0 }, // 18/31 = 58%
  { 1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1,0 }, // 20/31 = 65%
  { 1,1,0,1,1,0,1,1,1,0,1,1,0,1,1,0,1,1,1,0,1,1,0,1,1,1,0,1,1,0,1 }, // 22/31 = 71%
  { 1,1,1,0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0,1,1,1,1,0,1,1 }, // 25/31 = 81%
  { 1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1 }  // 28/31 = 90%
} ;
#endif


// DBG
void break_here()
{
}

/* Write a control value into one of the gpio_ledstatus[8]
 * control value maybe a pattern for a brightness phase of the value
 */
static void value2gpio_ledstatus_value(blinkenlight_panel_t *p, blinkenlight_control_t *c,
		uint32_t value, volatile uint32_t *gpio_ledstatus)
{
	unsigned gpio_register_idx = c->blinkenbus_register_wiring[0].blinkenbus_register_address;
	unsigned bit_offset = c->blinkenbus_register_wiring[0].blinkenbus_lsb;
	unsigned mask = (BitmaskFromLen32[c->value_bitlen] << bit_offset);
	unsigned gpio_bits;

	// write value to gpio's
	gpio_bits = (value << bit_offset);
	switch (p->mode) {
	case RPC_PARAM_VALUE_PANEL_MODE_NORMAL:
		gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask) | gpio_bits;
		break;
	case RPC_PARAM_VALUE_PANEL_MODE_LAMPTEST:
		// all LEDs on, but do not change control values
		gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask) | mask;
		break;
	case RPC_PARAM_VALUE_PANEL_MODE_ALLTEST:
		// all LEDs on, but do not change control values
		gpio_ledstatus[gpio_register_idx] = (gpio_ledstatus[gpio_register_idx] & ~mask) | mask;
		break;
	case RPC_PARAM_VALUE_PANEL_MODE_POWERLESS:
		// all LEDs off, but do not change control values
		gpio_ledstatus[gpio_register_idx] = 0;
		break;
	}
}

/*
 * - averages the Blinkenlight API outputs,
 * - generates the LED brightness patterns
 * - swaps the double buffer
 *
 * gpiopattern_blinkenlight_panel must be set before call!
 *
 * This here should run in a low-frequency thread.
 * recommended frequency: 50 Hz, much lower than SimH update rate
 *
 */
void *gpiopattern_update_leds(int *terminate)
{

	while (*terminate == 0) {
		blinkenlight_panel_t *p = gpiopattern_blinkenlight_panel; // short alias
		uint64_t now_us;
		unsigned i;

		// wait for one period
		nanosleep((struct timespec[]
		) {	{	0, GPIOPATTERN_UPDATE_PERIOD_US * 1000}}, NULL);

		if (p == NULL)
			continue;

		now_us = historybuffer_now_us();

		// mount values for gpio_registers ordered by register,
		// else flicker by co-running gpio_mux may occur.
		for (i = 0; i < p->controls_count; i++) {
			blinkenlight_control_t *c = &p->controls[i];
			unsigned bitidx;
			unsigned phase;
			if (c->is_input)
				continue;

			// fetch  shift
			// get averaged values
			historybuffer_get_average_vals(c->history, AVERAGING_INTERVAL_US, now_us, /*bitmode*/1);


			/*
			 // construct un-averaged value
			 for (bitidx = 0; bitidx < c->value_bitlen; bitidx++)
			 c->averaged_value_bits[bitidx] = (bitidx + 1) * 8 * ((c->value >> bitidx) & 1);
			 //MSB s brighter
			 */

			// build the display value from the low-passed bits.
			// for all display phases :
			for (phase = 0; phase < GPIOPATTERN_LED_BRIGHTNESS_PHASES; phase++) {
				unsigned value;

				// mount phase value
				volatile uint32_t *gpio_ledstatus = // alias
						gpiopattern_ledstatus_phases[gpiopattern_ledstatus_phases_writeidx][phase];
				value = 0;
				// for all bits :
				for (bitidx = 0; bitidx < c->value_bitlen; bitidx++) {
					// mount phase bit
					unsigned bit_brightness = ((unsigned) (c->averaged_value_bits[bitidx])
							* (GPIOPATTERN_LED_BRIGHTNESS_LEVELS)) / 256; // from 0.. 255 to

					assert(bit_brightness < GPIOPATTERN_LED_BRIGHTNESS_LEVELS);
					if (brightness_phase_lookup[bit_brightness][phase])
						value |= 1 << bitidx;

				}
				value2gpio_ledstatus_value(p, c, value, gpio_ledstatus); // fill in to gpio
			}
		}

		// switch pages of double buffer
		gpiopattern_ledstatus_phases_readidx = gpiopattern_ledstatus_phases_writeidx;
		gpiopattern_ledstatus_phases_writeidx = !gpiopattern_ledstatus_phases_writeidx;

	} // while(! terminate)
	return 0;
}

