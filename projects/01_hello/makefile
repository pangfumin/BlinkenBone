#
# CC Command
#

OS_CCDEFS = -lrt -lm -D_GNU_SOURCE

# PATH=/home/joerg/angstrom_distro/setup-scripts/build/tmp-angstrom_2010_x-eglibc/sysroots/i686-linux/usr/bin/armv7a-angstrom-linux-gnueabi:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games

# fuer beaglebone und sourcery codebench lite
#CCARM = arm-none-linux-gnueabi-gcc -mthumb -march=armv7-a -std=c99 -U__STRICT_ANSI__ -O2 -g $(OS_CCDEFS) -I.
CCARM = arm-angstrom-linux-gnueabi-gcc -mthumb -march=armv7-a -std=c99 -U__STRICT_ANSI__ -O2 -g $(OS_CCDEFS) -I.
CCX64 = gcc -std=c99 -m64 -U__STRICT_ANSI__ -O2 -g $(OS_CCDEFS) -I.
CCX86 = gcc -std=c99 -m32 -U__STRICT_ANSI__ -O2 -g $(OS_CCDEFS) -I.
CCRPI = arm-linux-gnueabihf-gcc -O2 -g $(OS_CCDEFS)



#
# Build everything
#


all:    hello_x64 hello_x86 hello_bb hello_rpi

clean:
	rm hello_x64 hello_x86 hello_bb hello_rpi

hello_x64:	makefile hello.c
	${CCX64} hello.c -ggdb3 -O0 -o $@
	file $@

hello_x86:	makefile hello.c
	${CCX86} hello.c -ggdb3 -O0 -o $@
	file $@

hello_bb:	makefile hello.c
#	${CC} hello.c -o $@
	${CCARM} hello.c -ggdb3 -O0 -o $@
	file $@
#	scp hello root@beaglebone:

hello_rpi: makefile hello.c
	${CCRPI} hello.c -ggdb3 -O0 -o $@
	file $@

