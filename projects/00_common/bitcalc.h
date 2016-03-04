 /* bitcalc.h:  utilities to deal with bits and masks

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


   14-Feb-2012  JH      created
*/

#ifndef BITCALC_H_
#define BITCALC_H_

#if defined(_MSC_VER)
#include <stdint.h>
typedef uint64_t	u_int64_t;
typedef uint32_t	u_int32_t;
// #define u_int64_t and int32_t in cmd line !
#endif

#include <sys/types.h> 	// u_int64_t
#if !defined(BITCALC_C_)
extern unsigned char BitmaskFromLen8[9];
extern u_int32_t BitmaskFromLen32[33];
extern u_int64_t BitmaskFromLen64[65];
extern unsigned char BitmaskReversed[256];
extern unsigned char BitsMirrored[256] ;

extern int DecimalDigitLenFromLen64[65] ;

#endif

int find_lowest_bit64(u_int64_t value, int bitval);
int get_msb_index64(u_int64_t value);

u_int64_t mount_bits_to_mask64(u_int64_t buffer, u_int64_t bitmask, int bitmask_leftshift,
		int buffer_bitoffset);

void encode_uint64_to_bytes(unsigned char *buffer, u_int64_t value, unsigned bytecount);
u_int64_t decode_uint64_from_bytes(unsigned char *buffer, unsigned bytecount);

int digitcount_from_bitlen(int radix, int bitlen) ;


#endif /* BITCALC_H_ */
