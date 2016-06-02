# Prototypic makefile for multi-architecture builds

ifeq ($(MAKE_CONFIGURATION),RELEASE)
CC_DBG_FLAGS = -O2 -Wno-unused-result
else
CC_DBG_FLAGS = -ggdb3 -O0
endif
OS_CCDEFS =

LDFLAGS= -lrt -lm -pthread


ifeq ($(MAKE_TARGET_ARCH),BEAGLEBONE)
	#### !!! when cross compiling !!!!
	# fuer beaglebone und angstrom stand alone toolchain
	CC=arm-angstrom-linux-gnueabi-gcc -mthumb -march=armv7-a -std=c99 -U__STRICT_ANSI__ $(CC_DBG_FLAGS) $(OS_CCDEFS) -I.
	OBJDIR=bin-beaglebone
else ifeq ($(MAKE_TARGET_ARCH),RPI)
	CC=arm-linux-gnueabihf-gcc -std=c99 -U__STRICT_ANSI__ $(CC_DBG_FLAGS) $(OS_CCDEFS) -I.
	OBJDIR=bin-rpi
else ifeq ($(MAKE_TARGET_ARCH),X86)
    OS_CCDEFS=-m32 $(CC_DBG_FLAGS)
	OBJDIR=bin-ubuntu-x86
else ifeq ($(MAKE_TARGET_ARCH),X64)
    OS_CCDEFS=-m64 $(CC_DBG_FLAGS)
	OBJDIR=bin-ubuntu-x64
else
	$error("MAKE_TARGET_ARCH not set!")
endif


# includes and defines go here
CCDEFS= \
	-I.	\
	-D_GNU_SOURCE	\
	$(CC_DBG_FLAGS) $(OS_CCDEFS)


SOURCES.h =

SOURCES.c = \
	hello.c

OBJECTS = $(SOURCES.c:%.c=%.o)

# assembly listings, see http://www.delorie.com/djgpp/v2faq/faq8_20.html
# always with symbol info (-ggdb), without link stage (-c)
%.lst:	%.c
	${CC} $(CCDEFS) -ggdb3 -c -Wa,-a,-ad $^ >$@ 
	rm -f a.out *.o  ; mkdir -p $(OBJDIR) ; mv $@ $(OBJDIR)


clean:
	rm -f a.out core $(OBJDIR)/*.lst hello $(OBJDIR)/hello $(OBJECTS)

all: hello

hello:	$(SOURCES.c) $(SOURCES.h)
	${CC} $^ -o $@ $(CCDEFS) ${LDFLAGS}
	pwd
	# move to architecture out dir and check .. was it x86 or ARM or waht?
	mkdir -p $(OBJDIR)
	mv $@ $(OBJDIR)
	file $(OBJDIR)/$@
