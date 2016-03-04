/* menus.c: user menus for panels and controls

   Copyright (c) 2013-2016, Joerg Hoppe
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

   20-Feb-2016  JH      added PANEL_MODE_POWERLESS
   23-Feb-2013  JH      created
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "radix.h"
#include "mcout.h"
#include "inputline.h"

#include "actions.h"

#include "main.h"

#ifdef WIN32
#include <windows.h>
#define strcasecmp _stricmp	// grmbl
#else
#include <unistd.h>
#endif

// width of screen for menu display
int menu_linewidth;

/**********************************************
 * User Interface
 *********************************************/

/*
 * read character string from stdin
 */
static char *getchoice()
{
	static char s_choice[255];

	inputline(s_choice, sizeof(s_choice));
	//char *s;
	// do {
	// s_choice[0] = '\0'; //  empty buffer.
	// fgets(s_choice, sizeof(s_choice), stdin);
	// clr \n
	//for (s = s_choice; *s; s++)
	//	if (*s == '\n')
	//		*s = '\0';
	// } while (strlen(s_choice) == 0); //loop until real input
	return s_choice;
}

/**********************************************
 *	Display controls of a panels
 *
 *	- state of inputs is displayed
 *	- inputs can be polled for change
 *	- outputs can be set
 */
static void menu_controls(blinkenlight_panel_t *p)
{
	int ready;
	unsigned i_control;
	int outcontrol_menu_id;
	unsigned name_len;
	blinkenlight_control_t *c;
	mcout_t mcout; // Multi Column OUTput
	char *s_choice;

	ready = 0;
	do {
		// clear "change" marker
		for (i_control = 0; i_control < p->controls_count; i_control++) {
			c = &(p->controls[i_control]);
			c->value_previous = c->value;
		}
		// query input values from server
		if (blinkenlight_api_client_get_inputcontrols_values(blinkenlight_api_client, p) != 0) {
			fputs(blinkenlight_api_client_get_error_text(blinkenlight_api_client), stderr);
			exit(1); // error
		}
		printf("\n");
		printf("\n");
		printf("*** Controls of panel \"%s\" ***\n", p->name);
		for (name_len = i_control = 0; i_control < p->controls_count; i_control++) {
			c = &(p->controls[i_control]);
			if (c->is_input && name_len < strlen(c->name))
				name_len = strlen(c->name);
		}
		// first input controls, with value, but without item selectors
		printf("* Inputs:\n");
		mcout_init(&mcout, MAX_BLINKENLIGHT_PANEL_CONTROLS);
		for (i_control = 0; i_control < p->controls_count; i_control++) {
			c = &(p->controls[i_control]);
			if (c->is_input) {
				mcout_printf(&mcout, "%-*s (%2d bit %-7s)=%s%s%s", name_len, c->name,
						c->value_bitlen, blinkenlight_control_type_t_text(c->type),
						radix_u642str(c->value, c->radix, c->value_bitlen, 0),
						radix_getname_char(c->radix), //o, h, d
						c->value_previous != c->value ? " !" : "  ");
			}

		}
		mcout_flush(&mcout, stdout, menu_linewidth, "  ||  ", /*first_col_then_row*/0);

		for (name_len = i_control = 0; i_control < p->controls_count; i_control++) {
			c = &(p->controls[i_control]);
			if (!c->is_input && name_len < strlen(c->name))
				name_len = strlen(c->name);
		}
		printf("* Outputs:\n");
		mcout_init(&mcout, MAX_BLINKENLIGHT_PANEL_CONTROLS);
		// select numbers = sequential count of output controls
		for (outcontrol_menu_id = i_control = 0; i_control < p->controls_count; i_control++) {
			c = &(p->controls[i_control]);
			if (!c->is_input) {
				mcout_printf(&mcout, "%2d) %-*s (%2d bit %-7s)=%s%s", outcontrol_menu_id, name_len,
						c->name, c->value_bitlen, blinkenlight_control_type_t_text(c->type),
						radix_u642str(c->value, c->radix, c->value_bitlen, 0),
						radix_getname_char(c->radix)); //o, h, d
				// mark control with place in display list
				c->tag = outcontrol_menu_id;
				outcontrol_menu_id++;
			}
		}
		mcout_flush(&mcout, stdout, menu_linewidth, "  ||  ", /*first_col_then_row*/0);

		mcout_init(&mcout, 20);

		mcout_printf(&mcout, "\"<id> <value>\" - set output manually");
		mcout_printf(&mcout, "\"<id> mo\"      - \"moving ones\"");
		mcout_printf(&mcout, "\"<id> mz\"      - \"moving zeros\"");
		mcout_printf(&mcout, "\"<id> i\"       - \"invert bits\"");
		mcout_printf(&mcout, "\"<id> dl <period>\" - \"dim LSBs, period in ms\"");
		mcout_printf(&mcout, "\"<id> dh <period>\" - \"dim MSBs\"");
		mcout_printf(&mcout, "<id>: number, name or \"*\"");
		mcout_printf(&mcout, " q)  quit");
		mcout_flush(&mcout, stdout, menu_linewidth, "  ||  ", /*first_col_then_row*/0);
		printf("Examples: \"1 300\" > set bits 9&10 of control 1; \"* 0\" => clear all; \"* mo\" => shift bit through all\n") ;
		printf(">>> ");
		s_choice = getchoice();
		if (strlen(s_choice) == 0) {
			// should not happen, but occurs under Eclipse?
		} else if (!strcasecmp(s_choice, "q")) {
			ready = 1;
			//} else if (!strcasecmp(s_choice, "t")) {
			// toggle selftest
		} else { // parse <nr> [<hex value>]
			char controlnr_buffer[80];
			char action_buffer[80];
			char action_args_buffer[80];
			int n_fields;
			int selected_outcontrol_menu_id;
			int user_abort;

			// check: a single number, a name, or a '*'?
			n_fields = sscanf(s_choice, "%s %s %s", controlnr_buffer, action_buffer, action_args_buffer);

			if (!strcmp(controlnr_buffer, "*")) {
				selected_outcontrol_menu_id = -1; // operate on all output controls
			} else {
				// check: an valid output control name?
				c = blinkenlight_panels_get_control_by_name(blinkenlight_api_client->panel_list, p,
						controlnr_buffer, /*!is_input*/0);
				if (c)
					selected_outcontrol_menu_id = (int) c->tag; // get place in display
				else
					// try number
					selected_outcontrol_menu_id = atoi(controlnr_buffer);
			}

			if (n_fields < 2) { // no <action> entered
				printf("new action or %s value for output control %s (%d significant bits): ",
						radix_getname_short(c->radix), c->name, c->value_bitlen);
				scanf("%s", action_buffer);
				printf("\n");
			}

			/* logic: iterate always over all controls,
			 * do action for all controls, or only for the selected one
			 */
			for (user_abort = i_control = 0; !user_abort && i_control < p->controls_count;
					i_control++) {
				c = &(p->controls[i_control]);
				if (!c->is_input) {
					if (selected_outcontrol_menu_id < 0) {
						// do for all output controls
						printf("%s ", c->name); // log operation
						user_abort = !do_output_control_action(p, c, action_buffer, action_args_buffer);
						printf("\n");
					} else if (selected_outcontrol_menu_id == c->tag) {
						do_output_control_action(p, c, action_buffer, action_args_buffer);
					}
				}
			}
		}
	} while (!ready);

}

/**********************************************
 *	Display available panels
 */
void menu_panels()
{
	int ready;
	unsigned i_panel;
	blinkenlight_panel_t *p;
	char *s_choice;
	char opcode[80];
	int numarg;
	unsigned blinkenboards_enable_state = 0;
	unsigned panel_mode = 0;
	int n_fields;

	ready = 0;
	do {
		printf("\n");
		printf("\n");
		printf("*** Select a panel from server on host \"%s\" ***\n",
				blinkenlight_api_client->rpc_server_hostname);
		for (i_panel = 0; i_panel < blinkenlight_api_client->panel_list->panels_count; i_panel++) {
			p = &(blinkenlight_api_client->panel_list->panels[i_panel]);
			printf("%d)     Panel \"%s\" \n", i_panel, p->name);
			if (strlen(p->info))
				printf("    Info: \"%s\"\n", p->info);
			printf("       %u controls: %u inputs + %u outputs\n", p->controls_count,
					p->controls_inputs_count, p->controls_outputs_count);
			{ // count the wires
				unsigned input_bit_count = 0, output_bit_count = 0;
				unsigned i_control;
				blinkenlight_control_t *c;
				for (i_control = 0; i_control < p->controls_count; i_control++) {
					c = &(p->controls[i_control]);
					if (c->is_input)
						input_bit_count += c->value_bitlen;
					else
						output_bit_count += c->value_bitlen;
				}
				printf("       Total %u value bits: %u inputs + %u outputs\n",
						input_bit_count + output_bit_count, input_bit_count, output_bit_count);
			}

			printf("       Value data stream: all inputs = %d bytes, all outputs %d bytes.\n",
					p->controls_inputs_values_bytecount, p->controls_outputs_values_bytecount);

			if (blinkenlight_api_client_get_object_param(blinkenlight_api_client,
					&blinkenboards_enable_state, RPC_PARAM_CLASS_PANEL, p->index,
					RPC_PARAM_HANDLE_PANEL_BLINKENBOARDS_STATE) != RPC_ERR_OK)
				printf("       ERROR detecting state of BlinkenBoards\n");
			if (blinkenlight_api_client_get_object_param(blinkenlight_api_client, &panel_mode,
					RPC_PARAM_CLASS_PANEL, p->index, RPC_PARAM_HANDLE_PANEL_MODE) != RPC_ERR_OK)
				printf("       ERROR detecting panel mode\n");

			printf("       State of BlinkenBoards Enable=%s",
					blinkenboards_enable_state == RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE ?
							"ACTIVE" :
							(blinkenboards_enable_state
									== RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_TRISTATE ?
									"TRISTATE" : "OFF"));
			{
				char *s[4] = { "normal", "historic lamp test", "full test", "powerless" };
				printf(". Panel mode = %u = \"%s\"\n", panel_mode, s[panel_mode]);
			}

		}
		printf("e <n>  'enable': toggle tristate of BlinkenBoards assigned to panel <n>\n");
		printf("t <n>  'test': circulate lamp & switch test modes for panel <n>\n");
		printf("i)     print info about server\n");
		printf("q)     quit\n");
		printf(">>> ");
		s_choice = getchoice();

		n_fields = sscanf(s_choice, "%s %d", opcode, &numarg);
		if (n_fields > 0) {
			if (!strcasecmp(opcode, "q")) {
				ready = 1;
			} else if (!strcasecmp(opcode, "i")) {
				char buffer[1024];
				if (blinkenlight_api_client_get_serverinfo(blinkenlight_api_client, buffer,
						sizeof(buffer)) != 0) {
					fputs(blinkenlight_api_client_get_error_text(blinkenlight_api_client), stderr);
					exit(1); // error
				};
				printf("%s\n", buffer); // multiline string
			} else if (!strcasecmp(opcode, "e") && n_fields == 2) {
				if (!blinkenlight_api_client_get_object_param(blinkenlight_api_client,
						&blinkenboards_enable_state, RPC_PARAM_CLASS_PANEL, numarg,
						RPC_PARAM_HANDLE_PANEL_BLINKENBOARDS_STATE)) {
					if (blinkenboards_enable_state
							== RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE)
						blinkenboards_enable_state =
								RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_TRISTATE;
					else
						blinkenboards_enable_state =
								RPC_PARAM_VALUE_PANEL_BLINKENBOARDS_STATE_ACTIVE;
					// read ok: write back reversed.
					blinkenlight_api_client_set_object_param(blinkenlight_api_client,
							RPC_PARAM_CLASS_PANEL, numarg,
							RPC_PARAM_HANDLE_PANEL_BLINKENBOARDS_STATE, blinkenboards_enable_state);
				}
			} else if (!strcasecmp(opcode, "t") && n_fields == 2) {
				if (!blinkenlight_api_client_get_object_param(blinkenlight_api_client, &panel_mode,
						RPC_PARAM_CLASS_PANEL, numarg, RPC_PARAM_HANDLE_PANEL_MODE)) {
					panel_mode++; // circulate 0, 1, 2, 3
					if (panel_mode > 3)
						panel_mode = 0;
					// read ok: write back reversed.
					blinkenlight_api_client_set_object_param(blinkenlight_api_client,
							RPC_PARAM_CLASS_PANEL, numarg, RPC_PARAM_HANDLE_PANEL_MODE, panel_mode);
				}
			} else if (isdigit(opcode[0])) {
				i_panel = strtol(opcode, NULL, 10);
				if (i_panel < blinkenlight_api_client->panel_list->panels_count)
					menu_controls(&(blinkenlight_api_client->panel_list->panels[i_panel]));
			}
		}
	} while (!ready);
}
