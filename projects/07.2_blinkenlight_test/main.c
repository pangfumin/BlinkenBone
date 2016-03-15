/* main.c: Test program for remote Blinkenlight API

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

   12-Mar-2016  JH      V 1.09  new C-like menu operators: ~, +, -, <, >
   08-Mar-2016  JH      V 1.08  better commandline processing with getopt2()
   01-Feb-2016  JH      V 1.07
                                actions to stimulated a "dimmed" control by high-speed
                                PWM output.
   10-Oct-2014  JH      V 1.06
                                found memoryleaks in blinkenlight_api_client.c:
                                responses from server must be free's with  "xdr_free()" !
   22-Feb-2013	JH      V 1.05
                                -w argument,
                                -f read file input
   09-Dec-2012  JH      created
*/

#define MAIN_C_



#define VERSION	"v1.09"
#define COPYRIGHT_YEAR	2016

#include <stdio.h>
#include <stdlib.h>
//#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "getopt2.h"
#include "inputline.h"

#include "blinkenlight_api_client.h" /* interface to RPC wrappers */

#ifdef WIN32
#include <windows.h>
#define strcasecmp _stricmp	// grmbl
#else
#include <unistd.h>
#endif

#include "actions.h"
#include "menus.h"
#include "main.h"

//// global API client
blinkenlight_api_client_t *blinkenlight_api_client;

// command line args
getopt_t	getopt_parser;

int arg_ping_repeat = 0;
char arg_cmdfilename[256];
char arg_hostname[256];

int arg_menu_linewidth = 132; // for screen output

/*
 * help()
 */
static void help()
{
	fprintf(stdout, "Command line summary:\n\n");
	// getop must be intialized to print the syntax
	getopt_help(&getopt_parser, stdout, arg_menu_linewidth, 10, "blinkenlightapitst");
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
	getopt_help_option(&getopt_parser, stdout, 96, 10);
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
	arg_cmdfilename[0] = 0;
	arg_hostname[0] = 0;

	getopt_def(&getopt_parser, NULL, NULL, "hostname", NULL, "Connect to the Blinkenlight API server on <hostname>\n"
		"<hostname> may be numerical or ar DNS name",
		"127.0.0.1", "connect to the server running on the same machine.",
		"raspberrypi", "connected to a RaspberryPi with default name.");
	getopt_def(&getopt_parser, "?", "help", NULL, NULL, "Print help", NULL, NULL, NULL, NULL);
	getopt_def(&getopt_parser, "cf", "cmdfile", "cmdfilename", NULL, "File from which commands are read.\n"
		"Lines are processed as if typed in.",
		"testseq", "read commands from file \"testseq\" and execute line by line", NULL, NULL);
	getopt_def(&getopt_parser, "p", "ping", "pingcount", NULL, "Ping server on <hostname> wether he is alife.\n"
		"There are <pingcount> tries. On error, exit status 1 is returned.",
		"20", "Ping for 20 seconds.", NULL, NULL);
	getopt_def(&getopt_parser, "w", "width", "columns", NULL, "set screen width for display.\n"
		"Panel controls are displayed in a table, which uses this much char columns.\n"
		"Should be less or equal to terminal window width. Minimum 80, default = 132.",
		"96", "Use 96 columns in terminal window", NULL, NULL);
	if (argc < 2)
		help(); // at least 1 required

	res = getopt_first(&getopt_parser, argc, argv);
	while (res > 0) {
		if (getopt_isoption(&getopt_parser, "help")) {
			help();
		}
		else if (getopt_isoption(&getopt_parser, "cmdfile")) {
			if (getopt_arg_s(&getopt_parser, "cmdfilename", arg_cmdfilename, sizeof(arg_cmdfilename)) < 0)
				commandline_option_error();
		}
		else if (getopt_isoption(&getopt_parser, "ping")) {
			if (getopt_arg_i(&getopt_parser, "pingcount", &arg_ping_repeat) < 0)
				commandline_option_error();
		}
		else if (getopt_isoption(&getopt_parser, "width")) {
			if (getopt_arg_i(&getopt_parser, "columns", &arg_menu_linewidth) < 0)
				commandline_option_error();
			if (arg_menu_linewidth < 80)
				arg_menu_linewidth = 80;
		}
		else if (getopt_isoption(&getopt_parser, NULL)) {
			if (getopt_arg_s(&getopt_parser, "hostname", arg_hostname, sizeof(arg_hostname)) < 0)
				commandline_option_error();
		}

		res = getopt_next(&getopt_parser);
	}
	if (res == GETOPT_STATUS_MINARGCOUNT || res == GETOPT_STATUS_MAXARGCOUNT)
		// known option, but wrong number of arguments
		commandline_option_error();
	else if (res < 0)
		commandline_error();
}
	

static void printheader() {
	printf("\n");
	printf(
		"*** blinkenlightapitst %s - client for BeagleBone Blinkenlight API panel interface ***\n",
		VERSION);
	printf("    Build " __DATE__ " " __TIME__ "\n");
	printf("    Copyright (C) 2012-%d Joerg Hoppe.\n", COPYRIGHT_YEAR);
	printf("    Contact: j_hoppe@t-online.de\n");
	printf("    Web: www.retrocmp.com/projects/blinkenbone\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	printheader();
	/*
	 * Save values of command line arguments
	 */

	parse_commandline(argc, argv);
	// returuns only if everything is OK

	if (arg_ping_repeat > 0) {
		if (ping(arg_hostname, arg_ping_repeat) == 0)
			exit(0);
		else
			exit(1);
	}

	menu_linewidth = arg_menu_linewidth;
	inputline_init();
	if (strlen(arg_cmdfilename) )
		// read commands from file
		inputline_fopen(arg_cmdfilename);

	blinkenlight_api_client = blinkenlight_api_client_constructor();

	printf("Connecting to %s ...\n", arg_hostname);
	if (blinkenlight_api_client_connect(blinkenlight_api_client, arg_hostname) != 0) {
		fputs(blinkenlight_api_client_get_error_text(blinkenlight_api_client), stderr);
		exit(1); // error
	}

	// load defined panels and controls from server
	if (blinkenlight_api_client_get_panels_and_controls(blinkenlight_api_client) != 0) {
		fputs(blinkenlight_api_client_get_error_text(blinkenlight_api_client), stderr);
		exit(1); // error
	}

	// show them, and let the user play
	menu_panels();

	blinkenlight_api_client_disconnect(blinkenlight_api_client);
	blinkenlight_api_client_destructor(blinkenlight_api_client);
}
