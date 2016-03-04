/* actions.c: implementation of user actions

 Copyright (c) 2012-2016, Joerg Hoppe
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


 25-Feb-2016  JH      ping failed on RPi, now first "connect", then "data request"
 09-Dec-2012  JH      created
 */

#include <stdlib.h>

#ifdef WIN32
#include <windows.h>
#include <time.h>
#define strcasecmp _stricmp	// grmbl
#define strncasecmp _strnicmp
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "kbhit.h"
#include "bitcalc.h"
#include "radix.h"
#include "blinkenlight_api_client.h"
#include "blinkenlight_panels.h"

/* from main */
extern blinkenlight_api_client_t *blinkenlight_api_client;
extern char *arg_hostname;

/*
 * update the output state of all controls
 */
void set_controls_outputs(blinkenlight_panel_t *p)
{
	// set output values to server
	if (blinkenlight_api_client_set_outputcontrols_values(blinkenlight_api_client, p) != 0) {
		fputs(blinkenlight_api_client_get_error_text(blinkenlight_api_client), stderr);
		exit(1); // error
	}
}

/*
 * get an output control by its index
 */
blinkenlight_control_t *get_output_control_by_index(blinkenlight_panel_t *p, int i_outcontrol)
{
	unsigned i_control;
	blinkenlight_control_t *c;

	// find again the "i_outcontrol"th output control
	for (i_control = 0; i_control < p->controls_count; i_control++) {
		c = &(p->controls[i_control]);
		if (!c->is_input) {
			if (i_outcontrol == 0) // stop if down count to 0, then blc valid
				break;
			i_outcontrol--;
		}
		c = NULL;
	}
	return c;
}

/*
 * shift a bit through all bitpositions in the control value.
 * with "delay_ms" delay between the phases
 * moving one = 1: 1 is shifted ("moving ones") , else 0 is shifted ("moving zeros")
 * This is stopped, if a key is hit.
 * result: 0 = finished
 * 1 = abort by user
 */
int control_moving_bit(blinkenlight_panel_t *p, blinkenlight_control_t *c, int delay_ms,
		int moving_one)
{
	int bitidx;
	u_int64_t saved_value, test_value;
	int user_break = 0;

	saved_value = c->value; // save control value
	for (bitidx = -1; !user_break && bitidx < (int) c->value_bitlen; bitidx++) {
		// bitidx -1: no bit set, initial "background" pattern
		printf(".");
		if (bitidx < 0)
			test_value = 0;
		else
			test_value = ((u_int64_t) 1 << bitidx);
		if (!moving_one)
			test_value = (~test_value) & BitmaskFromLen64[c->value_bitlen]; // moving zeros
		c->value = test_value;
		set_controls_outputs(p);

#ifdef WIN32
		Sleep(delay_ms);
#else
		usleep(1000 * delay_ms);
#endif
		if (os_kbhit())
			user_break = 1; //abort by user
	}
	c->value = saved_value; // restore control value
	set_controls_outputs(p);

	return user_break;
}

/*
 * send bits pulsing on/off to control to produce a "dimmed" view of lamps
 * the cycle is 16: lowest dim level is 1/16, highest is 15/16 "on"-time
 * "period" is the repeat interval in milli secs.
 * "fill_from_msb": if 0, LSB is darkest, higher bits become brighter until end of control or 16
 *	if 1, MSB is brightest, lower bits become darker
 * stop by any key
 *
 */
int control_dim(blinkenlight_panel_t *p, blinkenlight_control_t *c, int period_ms,
		int fill_from_msb)
{
	unsigned bit_idx, pattern_idx;
	int user_break = 0;
	u_int64_t saved_value = c->value; // save control value
	u_int32_t pattern[16];

	// 1) generate pattern:

	//	time	patternidx		bits[15:0]
	//	  |		0				11111..1111
	//	  |		1				11111..1110
	//	  |		2				11111..1100
	//	  |			...
	//	  V		13				11100..0000
	//			14				11000..0000
	//			15				10000..0000
	for (pattern_idx = 0; pattern_idx < 16; pattern_idx++)
		pattern[pattern_idx] = (~BitmaskFromLen32[pattern_idx]) & BitmaskFromLen32[16];

#ifdef NONONO
	!!! Do not optimize the patterns: good challenge for test panel !!!

	// For the 16 patterns, in bit[i] there have to be 'i' 'one' bits.
	// These should be evenly distributed in the column ( = over time)
	for (bit_idx = 0; bit_idx < 16; bit_idx++) {
		// fill every bit column separate:
		// 1 "one" in bit col 0, 2 "one"s in bit 1, ... 8 ones in bit col 7
		double bit_dist = 16.0 / (bit_idx + 1);
		int bit_col_sum = 0;// sum of ones already in col 0..pattern_idx
		for (pattern_idx = 0; pattern_idx < 16; pattern_idx++) {
			if (bit_col_sum < xxxx(pattern_idx + 1)* bit_dist) {
				pattern[pattern_idx] |= (1 << bit_idx);
				bit_col_sum++;
			}
		}

	}
	for (pattern_idx = 0; pattern_idx < 16; pattern_idx++) {
		printf("pattern[%2d] = 0x%04x = ", pattern_idx, pattern[pattern_idx]);
		for (bit_idx = 0; bit_idx < 16; bit_idx++)
		printf("%d", (pattern[pattern_idx] >> bit_idx) & 1);
		printf("\n");

	}
#endif
	pattern_idx = 0;
	while (!user_break) {
		u_int32_t tmppattern = 0;
		pattern_idx = (pattern_idx + 1) % 16;
		tmppattern = pattern[pattern_idx];

		// 2) display pattern on control
		// move bits 15..0 of pattern either
		// lsb or smb aligned to control
		if (fill_from_msb) {
			if (c->value_bitlen < 16)
				// too few targets bits: trunc pattern LSBs
				tmppattern >>= 16 - c->value_bitlen;
			else
				// too much target bits: shift pattern onto target MSB
				tmppattern <<= c->value_bitlen - 16;
		}
		// else LSB aligned: simply write masked
		c->value = tmppattern & BitmaskFromLen64[c->value_bitlen];
		set_controls_outputs(p);

#ifdef WIN32
		Sleep(period_ms);
#else
		usleep(1000 * period_ms);
#endif

		if (os_kbhit())
			user_break = 1; //abort by user
	}
	c->value = saved_value; // restore control value
	set_controls_outputs(p);

	return user_break;
}

/**********************************************
 *	ping()
 *	ping server n-times,
 *	then exit(0) (found) or 1 (failed)
 */
int ping(char *hostname, int repeat_count)
{
	int found = 0;
	printf("Pinging Blinkenlight API server on host \"%s\" ...\n", hostname);
	blinkenlight_api_client = blinkenlight_api_client_constructor();
	while (!found && repeat_count) {
		repeat_count--;
		if (blinkenlight_api_client_connect(blinkenlight_api_client, hostname) != 0)
			printf("RPC connect failed.\n");
		else if (blinkenlight_api_client_get_panels_and_controls(blinkenlight_api_client) != 0)
			printf("Server not responding.\n");
		else
			found = 1;
		blinkenlight_api_client_disconnect(blinkenlight_api_client);
		if (!found) {

#ifdef WIN32
			Sleep(1000); // wait 1 sec.
#else
			sleep(1);
#endif
		}
	}
	if (found)
		printf("Blinkenlight API server on host \"%s\" is alive.\n", hostname);

	blinkenlight_api_client_destructor(blinkenlight_api_client);

	return !found; // 1 = error, 0 = OK
}

/*
 * perform an action for an output control
 * action can be:
 * "number" -> setcontrol value to number
 * "mo" -> do a moving one bit on every value bit
 * "mz" -> do a moving zero bit on every value bit
 * "i" -> invert all bits
 * "dl <period>": "dimm low" show on control bits the low dim levels for 1 sec
 *                bit 0 is darkest, higher bit become brighter
 *                <period> is the pulse duration in milli secs
 * "dh <period>": "dim high" show on control bits the high dim levels for 1 sec
 *                MSB is brightest, lower bit become darker
 *                <period> is the pulse duration in milli secs
 * result: 1 = OK, 0 = user abort
 */
int do_output_control_action(blinkenlight_panel_t *p, blinkenlight_control_t *c, char *action,
		char *action_args)
{
#define DELAY 250		// 4 lights/Sec
	u_int64_t value;
	int user_abort = 0;
	if (!strcasecmp(action, "mo")) {
		// special test: moving ones
		user_abort = control_moving_bit(p, c, /*delay ms*/DELAY, 1);
	} else if (!strcasecmp(action, "mz")) {
		// special test: moving zeros
		user_abort = control_moving_bit(p, c, /*delay ms*/DELAY, 0);
	} else if (!strcasecmp(action, "i")) {
		// invert all bits, trunc to valid
		c->value = ~c->value & BitmaskFromLen64[c->value_bitlen];
		set_controls_outputs(p);
	} else if (!strncasecmp(action, "dl", 2)) {
		char *endptr;
		int period = strtol(action_args, &endptr, 0); // syntax is "dl <period>"
		if (*endptr)
			period = 10; // default: 10 ms
		control_dim(p, c, period, /*fill_from_msb*/0);
	} else if (!strncasecmp(action, "dh", 2)) {
		char *endptr;
		int period = strtol(action_args, &endptr, 0); // syntax is "dl <period>"
		if (*endptr)
			period = 10; // default: 10 ms
		control_dim(p, c, period, /*fill_from_msb*/1);
	} else {
		// interpret value according to radix of control
		if (!radix_str2u64(&value, c->radix, action)) {
			fprintf(stderr, "Illegal %s value \"%s\" entered!\n", radix_getname_long(c->radix),
					action);
		} else {
			// trunc value to valid bits
			value &= BitmaskFromLen64[c->value_bitlen];
			c->value = value;

			set_controls_outputs(p);
		}
	}
	return !user_abort;
}

