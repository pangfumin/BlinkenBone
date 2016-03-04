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



#define VERSION	"v1.07"
#define COPYRIGHT_YEAR	2016

#include <stdio.h>
#include <stdlib.h>
//#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "getopt.h"
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
int arg_ping_repeat = 0;
char *arg_cmdfilename = NULL;
char *arg_hostname;

int arg_menu_linewidth = 132; // for screen output

/*
 * help()
 */
static void help()
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr,
			"blinkenlightapitst [-p <repeat_cout>] [-w screen_width] [-f <cmdfile>] <host>\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "-p: \"PING\". Try <repeat_count> to contact server on host <host>.\n");
	fprintf(stderr, "           If OK, exit with code 0, else exit with 1\n");
	fprintf(stderr, "-w: \"WIDTH\". Set width of screen in chars\n");
	fprintf(stderr, "-f: \"FILE\". Filename, from which commands are read\n");
	fprintf(stderr, "           Lines are processed as if typed in.\n");
	fprintf(stderr, "\n");
}

/*
 * read commandline parameters into global vars
 * result: 0 = OK, 1 = error
 */
static int parse_commandline(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "p:w:f:")) != -1)
		switch (c) {
		case 'p':
			arg_ping_repeat = strtol(optarg, NULL, 10);
			break;
		case 'w':
			arg_menu_linewidth = strtol(optarg, NULL, 10);
			if (arg_menu_linewidth < 80)
				arg_menu_linewidth = 80;
			break;
		case 'f':
			arg_cmdfilename = optarg;
			break;
		case '?': // getopt detected an error. "opterr=0", so own error message here
			if (optopt == 'c')
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
			else if (isprint(optopt))
				fprintf(stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
			return 1;
			break;
		default:
			abort(); // getopt() got crazy?
			break;
		}
	if (argc <= optind) {
		return 1; // hostname missing
	}
	arg_hostname = argv[optind];
	return 0;
}
/*
 *
 */
int main(int argc, char *argv[])
{
	/*
	 * Save values of command line arguments
	 */
	printf("\n");
	printf(
			"*** blinkenlightapitst %s - client for BeagleBone Blinkenlight API panel interface ***\n",
			VERSION);
	printf("    Build " __DATE__ " " __TIME__ "\n");
	printf("    Copyright (C) 2012-%d Joerg Hoppe.\n", COPYRIGHT_YEAR);
	printf("    Contact: j_hoppe@t-online.de\n");
	printf("    Web: www.retrocmp.com/projects/blinkenbone\n");
	printf("\n");

	if (parse_commandline(argc, argv) != 0) {
		help();
		exit(1);
	}

	if (arg_ping_repeat > 0) {
		if (ping(arg_hostname, arg_ping_repeat) == 0)
			exit(0);
		else
			exit(1);
	}

	menu_linewidth = arg_menu_linewidth;
	inputline_init();
	if (arg_cmdfilename != NULL )
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
