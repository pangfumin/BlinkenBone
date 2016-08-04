/* gpio.h: the real-time process that handles BlinkenBoard multiplexing

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


   25-May-2016  JH      created
*/


#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdio.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <fcntl.h> // extra

#define SWITCH_LOWPASS_FREQUENCY    10
// MAX_BLINKENLIGHT_HISTORY_ENTRIES in history buffer,
// updated with thread polling freqency !
// f=10Hz -> 100ms intervall -> 100 buffer entries used at 1 kHz -> OK

// multiplexer codes
#define MUX1    1   // gives some optical structure
#define MUX2    2
#define MUX3    3
#define MUX5    5
#define MUX6    6
#define MUX7    7



#ifndef _GPIO_C_
#endif



// thread main procedure
//void *blink(int *terminate) ;
// different type to use it for pthread_start

#endif
