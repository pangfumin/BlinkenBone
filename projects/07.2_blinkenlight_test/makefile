

# rpcgen ... "NEWSTYLE (multiple arguments)
#	and own "main()"

#
# CC Command
#

#OS_CCDEFS = -lrt -lm -D_GNU_SOURCE
ifeq ($(MAKE_CONFIGURATION),RELEASE)
CC_DBG_FLAGS = -O2 -Wno-unused-result
else
CC_DBG_FLAGS = -ggdb3 -O0
endif
#LIBS=-lm
OS_CCDEFS =


ifeq ($(MAKE_TARGET_ARCH),BBW)
	#### !!! when cross compiling !!!!
	# fuer beaglebone und angstrom stand alone toolchain
	CC=$(BBW_CC) -std=c99 -U__STRICT_ANSI__ $(CC_DBG_FLAGS) $(OS_CCDEFS) -I.
	OBJDIR=bin-bbw
else ifeq ($(MAKE_TARGET_ARCH),BBB)
	CC=$(BBB_CC) -std=c99 -U__STRICT_ANSI__ $(CC_DBG_FLAGS) $(OS_CCDEFS) -I.
	OBJDIR=bin-bbb
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


CCDEFS= \
	-DBLINKENLIGHT_CLIENT	\
	-I.	\
	-I$(BLINKENLIGHT_COMMON_DIR)	\
	-I$(BLINKENLIGHT_API_DIR)	\
	-I$(BLINKENLIGHT_API_DIR)/rpcgen_linux	\
	$(CC_DBG_FLAGS) $(OS_CCDEFS)


#########################################################
# sub dir for the API. is same level (= sibling) as server and client
BLINKENLIGHT_COMMON_DIR=../00_common
BLINKENLIGHT_API_DIR=../07.0_blinkenlight_api

# *_svc, *_xdr and api.h sources must be created with rpcgen
BLINKENLIGHT_API_SOURCES.c = \
	$(BLINKENLIGHT_API_DIR)/blinkenlight_panels.c	\
	$(BLINKENLIGHT_API_DIR)/blinkenlight_api_client.c	\
	$(BLINKENLIGHT_API_DIR)/rpcgen_linux/rpc_blinkenlight_api_clnt.c \
	$(BLINKENLIGHT_API_DIR)/rpcgen_linux/rpc_blinkenlight_api_xdr.c	\
	$(BLINKENLIGHT_COMMON_DIR)/radix.c	\
	$(BLINKENLIGHT_COMMON_DIR)/bitcalc.c \
	$(BLINKENLIGHT_COMMON_DIR)/getopt2.c \
	$(BLINKENLIGHT_COMMON_DIR)/kbhit.c	\
	$(BLINKENLIGHT_COMMON_DIR)/mcout.c	\
	$(BLINKENLIGHT_COMMON_DIR)/inputline.c


BLINKENLIGHT_API_SOURCES.h = \
	$(BLINKENLIGHT_API_DIR)/rpcgen_linux/rpc_blinkenlight_api.h	\
	$(BLINKENLIGHT_API_DIR)/blinkenlight_api_client.h	\
	$(BLINKENLIGHT_COMMON_DIR)/radix.h	\
	$(BLINKENLIGHT_COMMON_DIR)/bitcalc.h \
	$(BLINKENLIGHT_COMMON_DIR)/getopt2.h \
	$(BLINKENLIGHT_COMMON_DIR)/kbhit.h	\
	$(BLINKENLIGHT_COMMON_DIR)/mcout.h	\
	$(BLINKENLIGHT_COMMON_DIR)/inputline.h


$(BLINKENLIGHT_API_SOURCES.c) $(BLINKENLIGHT_API_SOURCES.h):
	# rpcgen
	make --directory=$(BLINKENLIGHT_API_DIR)/rpcgen_linux sources

#########################################################
SOURCES.h = \
	main.h	\
	menus.h	\
	actions.h	\
	$(BLINKENLIGHT_API_DIR)/blinkenlight_panels.h

SOURCES.c = \
	main.c	\
	menus.c	\
	actions.c	\
	$(BLINKENLIGHT_API_SOURCES.c)

OBJECTS = $(SOURCES.c:%.c=%.o)
#
# Build everything
#
all:    blinkenlightapitst

clean:
	rm -f core $(OBJDIR)/blinkenlighttst $(OBJECTS)
	make --directory=$(BLINKENLIGHT_API_DIR)/rpcgen_linux clean


blinkenlightapitst:	$(SOURCES.c) $(SOURCES.h)
	${CC} $^ -o $@ $(CCDEFS) ${LDFLAGS}
	# move to architecture out dir and check .. was it x86 or ARM?
	mkdir -p $(OBJDIR) ; mv $@ $(OBJDIR) ; file $(OBJDIR)/$@

