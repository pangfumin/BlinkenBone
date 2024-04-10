/* gpio.c: the real-time process that handles multiplexing

   Copyright (c) 2015-2016, Oscar Vermeulen & Joerg Hoppe
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


   16-Nov-2015  JH      created
*/


#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdio.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <fcntl.h> // extra

#include <stdio.h>
#include "modbus/modbus.h"
#include <errno.h>


//#define BCM2708_PERI_BASE       0x3f000000
//#define GPIO_BASE               (BCM2708_PERI_BASE + 0x200000)	// GPIO controller

#define BYTE_TO_BINARY_PATTERN "%c%c | %c%c%c | %c%c%c"
#define BYTE_TO_BINARY(byte)  \
  ((byte) & 0x80 ? '1' : '0'), \
  ((byte) & 0x40 ? '1' : '0'), \
  ((byte) & 0x20 ? '1' : '0'), \
  ((byte) & 0x10 ? '1' : '0'), \
  ((byte) & 0x08 ? '1' : '0'), \
  ((byte) & 0x04 ? '1' : '0'), \
  ((byte) & 0x02 ? '1' : '0'), \
  ((byte) & 0x01 ? '1' : '0') 

#define BLOCK_SIZE 		(4*1024)

// IO Acces
struct bcm2835_peripheral {
    unsigned long addr_p;
    int mem_fd;
    void *map;
    volatile unsigned int *addr;
};


#ifndef _GPIO_C_
//extern volatile unsigned int gpio_switchstatus[3] ; // bitfields: 3 rows of up to 12 switches
//extern volatile unsigned int gpio_ledstatus[8] ;	// bitfields: 8 ledrows of up to 12 LEDs
#endif


//struct bcm2835_peripheral gpio = {GPIO_BASE};


// thread main procedure
//void *blink(int *terminate) ;
// differnt type to use it for pthread_start

#endif
