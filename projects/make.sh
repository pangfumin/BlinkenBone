
#!/bin/bash


# stop on error
set -e

# Debugging:
# set -x


# needed packages:



# compile all binaries for all platforms
pwd
export MAKEOPTIONS=--silent
export MAKETARGETS="clean all"


(
# All classes and resources for all Java panels into one jar
sudo apt-get install ant openjdk-7-jdk
cd 09_javapanelsim
ant -f build.xml compile jar
)


(
# the Blinkenligt API server for BlinkenBus is only useful on BEAGLEBONE
# Simulation and syntax test modes work also on desktop Linuxes
cd 07.1_blinkenlight_server
MAKE_TARGET_ARCH=BEAGLEBONE make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=X86 make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=X64 make $MAKEOPTIONS $MAKETARGETS
)


(
# the Blinkenligt API server for Oscar Vermeulen's PiDP8
cd 11_pidp_server/pidp8
MAKE_TARGET_ARCH=RPI make $MAKEOPTIONS $MAKETARGETS
)
(
# the Blinkenligt API server for Oscar Vermeulen's PiDP11
cd 11_pidp_server/pidp11
MAKE_TARGET_ARCH=RPI make $MAKEOPTIONS $MAKETARGETS
)

(
# The Blinkenligt API test client for all platforms
cd 07.2_blinkenlight_test
MAKE_TARGET_ARCH=X86 make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=X64 make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=RPI make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=BEAGLEBONE make $MAKEOPTIONS $MAKETARGETS
)


(
# SimH for all platforms
cd 02.3_simh.4.x.jh/src
MAKE_TARGET_ARCH=X86 make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=X64 make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=BEAGLEBONE make $MAKEOPTIONS $MAKETARGETS
MAKE_TARGET_ARCH=RPI make $MAKEOPTIONS $MAKETARGETS
)

echo
echo "All OK!"
