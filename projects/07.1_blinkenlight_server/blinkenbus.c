/* blinkenbus.c: Access to BlinkenBus registers

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

 23-Feb-2016  JH      added PANEL_MODE_POWERLESS
 logic for input controls with constant value
 17-Feb-2012  JH      created
 */

#define BLINKENBUS_C_

// DEBUG_BLINKENBUSFILE_LOCAL
// debugging without running driver.
// if defined: to not use /dev/blinkenbus, but
//	open,close() - just printlog()
//	write()- just printlog() addresses and data
//	read()- geenerate data, printlog() addresses and data
//#define DEBUG_BLINKENBUSFILE_LOCAL

#include <stdlib.h>
#include <stdio.h>
#include <string.h>	// memcpy()
#include <limits.h>
#include <errno.h>
#ifndef WIN32	// no hardware access over /dev/blinkenbus, just simulation
#include <fcntl.h>	// open
#include <unistd.h>	// close
#endif
#include <assert.h>

#include "errno2txt.h"
#include "print.h"

#include "blinkenlight_panels.h"
#include "bitcalc.h"

#include "main.h" // global blinkenlight_panel_list
#include "blinkenbus.h" // own definitions
int blinkenbus_fd; // file descriptor for interface to driver


// mask with output registers marked as 0xff
static blinkenbus_map_t blinkenbus_map_used_output;
// mask with intput registers marked as 0xff
static blinkenbus_map_t blinkenbus_map_used_input;

// Output: cached state of registers
static blinkenbus_map_t blinkenbus_map_out_cache;

// input: bits read
static blinkenbus_map_t blinkenbus_map_in_cache;

#ifdef DEBUG_BLINKENBUSFILE_LOCAL
// simulated data for read() is incrementing sequence
unsigned debug_blinkenbusfile_local_read_data_source;
#endif
/*
 * Algorithm to output:
 * 1) init
 * 	blinkenbus_map_out_values_new[] := blinkenbus_map_out_cache[]
 * 2) set values
 * 	For all output controls:
 * 		encode values into blinkenbus_map_out_values_new[], overwrite old values
 * 3) output to device optimized
 * 		for all blinkenbus_map_out_values_new[i] != blinkenbus_map_out_cache[i]:
 * 			write blinkenbus_map_out_values_new[i] to file interface
 * 			blinkenbus_map_out_cache[i] := blinkenbus_map_out_values_new[i] ;
 * 4) output all
 *    do 3) for all registers in blinkenbus_map_out_register_mask[]
 */

/*
 * Algorithm to input
 * 1) init
 * 	clear blinkenbus_map_in_mask[]
 * 2) mark registers
 * 		for all controls:
 * 			set all value bits to '1', encode value to blinkenbus_map_in_mask[]
 * 			all bits part of an input value are now marked with a '1'
 * 3) read from device optimized
 * 	3.1) for all registers with at least on '1' in blinkenbus_map_in_mask[]:
 * 		- read value into blinkenbus_map_in_cache[]:
 * 	3.2) for all controls
 * 		decode blinkenbus_map_in_cache[] into register values,
 * 		set "value_previous"
 *
 */



/*
 * read value of board control register
 */
static unsigned char board_control_read(unsigned board_addr)
{
    unsigned regaddr;
    int ret_val;
    int bytes_read;
    unsigned char regval;

    assert(board_addr >= 0);
    assert(board_addr <= BLINKENBUS_MAX_BOARD_ADDR);

    regaddr = BLINKENBUS_ADDRESS_CONTROL(board_addr);

    print(LOG_DEBUG, "board_control_read() - reading board %d control registers at 0x%x\n",
            board_addr, regaddr);
#ifdef	DEBUG_BLINKENBUSFILE_LOCAL
    print(LOG_DEBUG, "  lseek(0x%x, SEEK_SET\n", (int) regaddr);
    regval = 0x30; //
    print(LOG_DEBUG, "  read() %d bytes: 0x%x\n", 1, regval);

#else
    ret_val = lseek(blinkenbus_fd, regaddr, SEEK_SET);
    if (ret_val < 0) {
        print(LOG_ERR, "board_control_read() - lseek() failed!\n");
        exit(1);
    }
    bytes_read = read(blinkenbus_fd, &regval, sizeof(regval));
    if (bytes_read != sizeof(regval)) {
        print(LOG_ERR, "board_control_read() - read() failed\n");
        print(LOG_ERR, "  bytes to read: %d, actual read: %d, errno %d = %s!\n", sizeof(regval),
                bytes_read, errno, errno2txt(errno));
        exit(1);
    }
#endif
    return regval;
}

/*
 * write the board control register
 */
static void board_control_write(unsigned board_addr, unsigned char regval)
{
    unsigned regaddr;
    int ret_val;
    int bytes_written;

    assert(board_addr >= 0);
    assert(board_addr <= BLINKENBUS_MAX_BOARD_ADDR);

    regaddr = BLINKENBUS_ADDRESS_CONTROL(board_addr);

    print(LOG_DEBUG, "board_control_write() -  writing board %d control registers at 0x%x\n",
            board_addr, regaddr);
#ifdef DEBUG_BLINKENBUSFILE_LOCAL
    print(LOG_DEBUG, "  lseek(0x%x, SEEK_SET\n", (int) regaddr);
    bytes_written = 1;
    print(LOG_DEBUG, "  write() %d bytes: 0x%x\n", bytes_written, regval);
#else

    ret_val = lseek(blinkenbus_fd, regaddr, SEEK_SET);
    if (ret_val < 0) {
        print(LOG_ERR, "board_control_write() - lseek() failed!\n");
        exit(1);
    }
    bytes_written = write(blinkenbus_fd, &regval, sizeof(regval));
#endif
    if (bytes_written != sizeof(regval)) {
        print(LOG_ERR, "board_control_write() - read() failed\n");
        print(LOG_ERR, "  bytes to write: %d, bytes written: %d, errno %d = %s!\n", sizeof(regval),
                bytes_written, errno, errno2txt(errno));
        exit(1);
    }
}

/*
 * write control value to register bits into cached blinkenbus address range
 * if "clear": write all value bits as 0
 * if "as_mask": write all value bits as 1
 */
void blinkenbus_control_to_cache(unsigned char *blinkenbus_cache, blinkenlight_control_t *c,
        int clear, int as_mask)
{
    unsigned i_register_wiring;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;
    uint64_t value;
    if (clear)
        value = 0;
    else if (as_mask)
        value = BitmaskFromLen64[c->value_bitlen]; // all '1'
    else if (c->mirrored_bit_order)
        value = mirror_bits(c->value, c->value_bitlen);
    else
        value = c->value;

    for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
            i_register_wiring++) {
        unsigned char regval; // value of current register
        unsigned char bitfield; // bits moutnend into current register
        // for all registers assigned whole or in part to control
        bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);

        // clear out used bits in register
        regval = blinkenbus_cache[bbrw->blinkenbus_register_address] & ~bbrw->blinkenbus_bitmask;

        bitfield = (value >> bbrw->control_value_bit_offset); // value shifted to register lsb
        if (bbrw->blinkenbus_levels_active_low)
            bitfield = ~bitfield;
        bitfield &= BitmaskFromLen8[bbrw->blinkenbus_bitmask_len]; // masked to register
        bitfield <<= bbrw->blinkenbus_lsb;

        blinkenbus_cache[bbrw->blinkenbus_register_address] = regval | bitfield; // write back
    }
}

/*
 * read control value from register bits from cached blinkenbus address range
 * set also "value_previous"
 */
void blinkenbus_control_from_cache(unsigned char *blinkenbus_cache, blinkenlight_control_t *c)
{
    unsigned i_register_wiring;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;
    uint64_t value;

    if (c->blinkenbus_register_wiring_count == 0)
        // dummy input with constant value: like POWER on 11/70
        value = c->value_default;
    else {
        value = 0;
        for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
                i_register_wiring++) {
            unsigned char regvalbits; // value of current register
            // for all registers assigned whole or in part to control
            bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);

            regvalbits = blinkenbus_cache[bbrw->blinkenbus_register_address];
            if (bbrw->blinkenbus_levels_active_low) //  inputs "low active"
                regvalbits = ~regvalbits;
            regvalbits &= bbrw->blinkenbus_bitmask; // bits of value, unshiftet
            regvalbits >>= bbrw->blinkenbus_lsb;
            // OR in the bits of current register
            value |= (uint64_t) regvalbits << bbrw->control_value_bit_offset;
        }
        if (c->mirrored_bit_order)
            value = mirror_bits(value, c->value_bitlen);
    }
    c->value_previous = c->value;
    c->value = value;
}

/*
 * write all output control values of a panel to cache
 * if mode = 1: selftest display
 * if mode = 3: all off
 * force_all: no optimization
 */
void blinkenbus_panel_to_cache(unsigned char *blinkenbus_cache, blinkenlight_panel_t *p)
{
    unsigned i_control;
    blinkenlight_control_t *c;
    print(LOG_DEBUG, "blinkenbus_panel_to_cache(panel=%s)\n", p->name);
    // lamp test: show light, but controls hold their state
    for (i_control = 0; i_control < p->controls_count; i_control++) {
        c = &(p->controls[i_control]);
        if (!c->is_input) {
            int all_ones = 0;
            int all_zeros = 0;
            if (p->mode == RPC_PARAM_VALUE_PANEL_MODE_ALLTEST
                    || (p->mode == RPC_PARAM_VALUE_PANEL_MODE_LAMPTEST && c->type == output_lamp))
                all_ones = 1; // selftest:
            else if (p->mode == RPC_PARAM_VALUE_PANEL_MODE_POWERLESS && c->type == output_lamp)
                all_zeros = 1; // all OFF

            blinkenbus_control_to_cache(blinkenbus_map_used_output, c, all_zeros, all_ones);
            // "write as mask": all ones, if self test
        }
    }

}

/* set all panel inputcontrols from cache */
void blinkenbus_panel_from_cache(unsigned char *blinkenbus_cache, blinkenlight_panel_t *p)
{
    unsigned i_control;
    blinkenlight_control_t *c;
// decode register values into control values
    for (i_control = 0; i_control < p->controls_count; i_control++) {
        c = &(p->controls[i_control]);
        if (c->is_input)
            blinkenbus_control_from_cache(blinkenbus_cache, c);
    }
}


/* init an cache with current output vaules,
 * before it is updated with new values
 */
void blinkenbus_cache_from_blinkenboards_outputs(unsigned char *blinkenbus_cache)
{
    memcpy(blinkenbus_cache, blinkenbus_map_out_cache, sizeof(blinkenbus_map_t)) ;
}


/*
 * write cache optimized to blinkenbus file device
 */
void blinkenbus_cache_to_blinkenboards_outputs(unsigned char *blinkenbus_cache, int force_all)
{
    // Output: new state of registers, to be written into file device
    static blinkenbus_map_t blinkenbus_map_out_values_new;

    unsigned i_control;
    unsigned regaddr, regaddr_block_start, regaddr_block_end;

    int ret_val;
    int bytes_to_write, bytes_written;
    unsigned char *data_block_start;

    print(LOG_DEBUG, "blinkenbus_cache_write()\n");

    /*
     * optimization: write neither whole blinkenbus address space
     * nor every single register, but the ranges of changed addresses,
     * - Too many data writes are slow,
     * - and too many file io procedures (lseek for every single register) are also slow.
     *
     * Good chance, that the controls of a panel are connected to adjacent registers!
     *
     * write only to registers marked as used outputs
     */

    regaddr_block_start = 0;
    while (regaddr_block_start <= BLINKENBUS_MAX_REGISTER_ADDR) {
// true if
#define OUTPUT_TO_UPDATE(regaddr) ( force_all \
	||	 (blinkenbus_cache[regaddr] != blinkenbus_map_out_cache[regaddr])	\
    )
        // 1) find start of next register block to write
        while (regaddr_block_start <= BLINKENBUS_MAX_REGISTER_ADDR //
        && (!blinkenbus_map_used_output[regaddr_block_start] // wait for valid output
        || !OUTPUT_TO_UPDATE(regaddr_block_start) // and changed
        ))
            regaddr_block_start++;
        // find end of register block
        regaddr_block_end = regaddr_block_start; // block_end NOT part of block
        while (regaddr_block_end <= BLINKENBUS_MAX_REGISTER_ADDR //
        && blinkenbus_map_used_output[regaddr_block_end] // must be valid output
                && OUTPUT_TO_UPDATE(regaddr_block_end) // and changed
        )
            regaddr_block_end++;
        // 2) write current block
        bytes_to_write = regaddr_block_end - regaddr_block_start;
        data_block_start = blinkenbus_map_out_values_new + regaddr_block_start;
        if (bytes_to_write > 0) {
            print_memdump(LOG_DEBUG, "  writing output registers:", regaddr_block_start,
                    bytes_to_write, data_block_start);
            //print(LOG_DEBUG, "  writing output registers 0x%x .. 0x%x\n", regaddr_block_start,
            //		regaddr_block_end - 1);

#ifdef DEBUG_BLINKENBUSFILE_LOCAL
            print(LOG_DEBUG, "  lseek(0x%x, SEEK_SET\n", (int) regaddr_block_start);
            {
                char buff[1024];
                char buff1[40];
                sprintf(buff, "  write() %d bytes: ", bytes_to_write);
                for (regaddr = regaddr_block_start; regaddr < regaddr_block_end; regaddr++)
                {
                    sprintf(buff1, "%02x ", (int) blinkenbus_map_out_values_new[regaddr]);
                    strcat(buff, buff1);
                }
                print(LOG_DEBUG, "%s\n", buff);
            }
            bytes_written = bytes_to_write;
#else

            ret_val = lseek(blinkenbus_fd, regaddr_block_start, SEEK_SET);
            if (ret_val < 0) {
                print(LOG_ERR, "blinkenbus_write_panel_output_controls() - lseek() failed!\n");
                exit(1);
            }
            bytes_written = write(blinkenbus_fd, data_block_start, bytes_to_write);
#endif
            if (bytes_written != bytes_to_write) {
                print(LOG_ERR, "blinkenbus_write_panel_output_controls() - write() failed\n");
                print(LOG_ERR, "  bytes to write: %d, written: %d, errno %d = %s!\n",
                        bytes_to_write, bytes_written, errno, errno2txt(errno));
                exit(1);
            }
        }

        // 3) find next block
        regaddr_block_start = regaddr_block_end + 1;
    }
    // update cache
    memcpy(blinkenbus_map_out_cache, blinkenbus_cache, sizeof(blinkenbus_map_t));
}

/*
 * read inputs for one panel from blinkenbus into cache
 */
void blinkenbus_cache_from_blinkenboards_inputs(unsigned char *blinkenbus_cache, blinkenlight_panel_t *p)
{
    blinkenbus_map_t blinkenbus_map_in_mask;
    // input: bits to be read are marked with 1
    unsigned regaddr;
    unsigned i_control;
    blinkenlight_control_t *c;
    unsigned i_register_wiring;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;
    unsigned regaddr_block_start, regaddr_block_end;
    int ret_val;
    unsigned bytes_to_read, bytes_read;

    print(LOG_DEBUG, "blinkenbus_read_panel_input_controls(panel=%s)\n", p->name);

    // 1) mark all used input registers
    for (regaddr = 0; regaddr <= BLINKENBUS_MAX_REGISTER_ADDR; regaddr++)
        blinkenbus_map_in_mask[regaddr] = 0;

    for (i_control = 0; i_control < p->controls_count; i_control++) {
        c = &(p->controls[i_control]);
        if (c->is_input) {
            for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
                    i_register_wiring++) {
                // mark all registers used by control
                bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);
                blinkenbus_map_in_mask[bbrw->blinkenbus_register_address] = 0xff;
            }
        }

    }
    // 2) load used registers in blocks into cache
    regaddr_block_start = 0;
    while (regaddr_block_start <= BLINKENBUS_MAX_REGISTER_ADDR) {
        // 1) find start of next register block to write
        while (regaddr_block_start <= BLINKENBUS_MAX_REGISTER_ADDR //
        && (!blinkenbus_map_used_input[regaddr_block_start] // wait for valid input
        || !blinkenbus_map_in_mask[regaddr_block_start] // and used by controls
        ))
            regaddr_block_start++;
        // find end of register block
        regaddr_block_end = regaddr_block_start; // block_end NOT part of block
        while (regaddr_block_end <= BLINKENBUS_MAX_REGISTER_ADDR //
        && blinkenbus_map_used_input[regaddr_block_end] // must be valid input
                && blinkenbus_map_in_mask[regaddr_block_end]) // and used by controls
            regaddr_block_end++;
        // 3) read current block
        bytes_to_read = regaddr_block_end - regaddr_block_start;
        if (bytes_to_read > 0) {
            print(LOG_DEBUG, "  reading input registers 0x%x .. 0x%x\n", regaddr_block_start,
                    regaddr_block_end - 1);
#ifdef DEBUG_BLINKENBUSFILE_LOCAL
            print(LOG_DEBUG, "  lseek(0x%x, SEEK_SET\n", (int) regaddr_block_start);
            {
                char buff[1024];
                char buff1[40];
                sprintf(buff, "  read() %d bytes: ", bytes_to_read);
                for (regaddr = regaddr_block_start; regaddr < regaddr_block_end; regaddr++)
                {
                    // inc values from gloabl sequence
                    blinkenbus_map_in_cache[regaddr] = (debug_blinkenbusfile_local_read_data_source++) & 0xff;
                    sprintf(buff1, "%02x ", (int) blinkenbus_map_in_cache[regaddr]);
                    strcat(buff, buff1);
                }
                print(LOG_DEBUG, "%s\n", buff);
            }
            bytes_read = bytes_to_read;
#else
            ret_val = lseek(blinkenbus_fd, regaddr_block_start, SEEK_SET);
            if (ret_val < 0) {
                print(LOG_ERR, "blinkenbus_read_panel_input_controls() - lseek() failed!\n");
                exit(1);
            }
            bytes_read = read(blinkenbus_fd, blinkenbus_map_in_cache + regaddr_block_start,
                    bytes_to_read);
#endif
            if (bytes_read != bytes_to_read) {
                print(LOG_ERR, "blinkenbus_read_panel_input_controls() - read() failed\n");
                print(LOG_ERR, "  bytes to read: %d, actual read: %d, errno %d = %s!\n",
                        bytes_to_read, bytes_read, errno, errno2txt(errno));
                exit(1);
            }
        }

        // 4) find next block
        regaddr_block_start = regaddr_block_end + 1;
    }
}

/*
 * in unsigned char blinkenbus_used_boards[BLINKENBUS_MAX_BOARD_ADDR + 1];
 * set a "1" for every board used by panel p
 * "used_boards" must be cleared by caller.
 *
 */
static void mark_boards_of_panel(unsigned char used_boards[], blinkenlight_panel_t *p)
{
    int i_control, i_register_wiring;
    blinkenlight_control_t *c;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;

    // for all register wirings of the panel:
    for (i_control = 0; i_control < p->controls_count; i_control++) {
        c = &(p->controls[i_control]);
        for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
                i_register_wiring++) {
            bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);
            used_boards[bbrw->blinkenbus_board_address] = 0xff;
        }
    }

}

/*
 * State of the BlinkenBoards ?
 * RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_OFF  		at least one BlinkenBoard of panel not found on bus
 * RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_TRISTATE 	at least one BlinkenBoard of panel disabled/tristate
 * RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE		all BlinkenBoards of panel active
 *
 *
 */
int blinkenbus_get_blinkenboards_state(blinkenlight_panel_t *p)
{
    // which boards to touch
    int result = RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE;
    int board_addr;
    unsigned char blinkenbus_used_boards[BLINKENBUS_MAX_BOARD_ADDR + 1];

    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        blinkenbus_used_boards[board_addr] = 0;

    mark_boards_of_panel(blinkenbus_used_boards, p);

    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        if (blinkenbus_used_boards[board_addr]) {
            unsigned char regval = board_control_read(board_addr);
            if (regval == 0xff) // empty bus address
                return RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_OFF;
            if (regval & 0x01) // "disable bit" for this board is set
                result = RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_TRISTATE; // not all boards enabled
        }
    return result;
}

/*
 * set the tristate of all BlinkenBoars for the named panel
 * Side effect for other panels on the same BlinkenBoards!
 * enable=0: all boards tristate
 */

void blinkenbus_set_blinkenboards_state(blinkenlight_panel_t *p, int new_state)
{
    // which boards to touch
    int board_addr;
    unsigned char blinkenbus_used_boards[BLINKENBUS_MAX_BOARD_ADDR + 1];

    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        blinkenbus_used_boards[board_addr] = 0;

    mark_boards_of_panel(blinkenbus_used_boards, p);

    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        if (blinkenbus_used_boards[board_addr]) {
            //enable/disable this board
            unsigned char regval;
            // disable reset bit in board control register
            regval = board_control_read(board_addr);
            if (new_state == RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE)
                regval &= ~0x01; // clear "disable" bit
            else
                regval |= 0x01; // all other: set "disable" bit
            board_control_write(board_addr, regval);
        }
}

/*
 * open file device,
 * init caches
 */
void blinkenbus_init()
{
    char devname[80];

    unsigned regaddr;
    unsigned i_panel;
    blinkenlight_panel_t *p;
    unsigned i_control;
    blinkenlight_control_t *c;
    unsigned i_register_wiring;
    blinkenlight_control_blinkenbus_register_wiring_t *bbrw;

    unsigned board_addr;
    unsigned char blinkenbus_used_boards[BLINKENBUS_MAX_BOARD_ADDR + 1];

    sprintf(devname, "/dev/%s", DEVICE_FILE_NAME);
    print(LOG_INFO, "Opening %s ...\n", devname);
#ifndef	DEBUG_BLINKENBUSFILE_LOCAL
    blinkenbus_fd = open(devname, O_RDWR | O_SYNC); // SYNC: wait if read/write calls completed

    if (blinkenbus_fd < 0) {
        print(LOG_ERR, "Could not open device file %s!\n", devname);
        exit(1);
    }
#endif
    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        blinkenbus_used_boards[board_addr] = 0;

    // mark all used output & input registers in blinkenbus map masks
    for (regaddr = 0; regaddr <= BLINKENBUS_MAX_REGISTER_ADDR; regaddr++) {
        blinkenbus_map_used_input[regaddr] = 0;
        blinkenbus_map_used_output[regaddr] = 0;
    }
    for (i_panel = 0; i_panel < blinkenlight_panel_list->panels_count; i_panel++) {
        p = &(blinkenlight_panel_list->panels[i_panel]);
        for (i_control = 0; i_control < p->controls_count; i_control++) {
            c = &(p->controls[i_control]);

            // reset all values
            c->value = c->value_default;
            c->value_previous = c->value;

            for (i_register_wiring = 0; i_register_wiring < c->blinkenbus_register_wiring_count;
                    i_register_wiring++) {

                bbrw = &(c->blinkenbus_register_wiring[i_register_wiring]);
                blinkenbus_used_boards[bbrw->blinkenbus_board_address] = 0xff; // mark board as used

                if (bbrw->board_register_space == input_register)
                    blinkenbus_map_used_input[bbrw->blinkenbus_register_address] = 0xff;
                if (bbrw->board_register_space == output_register)
                    blinkenbus_map_used_output[bbrw->blinkenbus_register_address] = 0xff;
            }
        }

    }
    // enable all blinkenbus boards
    for (board_addr = 0; board_addr <= BLINKENBUS_MAX_BOARD_ADDR; board_addr++)
        if (blinkenbus_used_boards[board_addr]) {
            unsigned char regval;
            // disable reset bit in board control register
            regval = board_control_read(board_addr);
            regval &= ~0x01; // clear disable bit
            board_control_write(board_addr, regval);
        }

    // initialize all panel output controls with default values
    for (i_panel = 0; i_panel < blinkenlight_panel_list->panels_count; i_panel++) {
        blinkenbus_map_t cache ;
        p = &(blinkenlight_panel_list->panels[i_panel]);
        blinkenbus_cache_from_blinkenboards_outputs(cache) ;
        blinkenbus_panel_to_cache(cache, p) ;
        blinkenbus_cache_to_blinkenboards_outputs(cache, /*force_all=*/1) ;
    }
}

