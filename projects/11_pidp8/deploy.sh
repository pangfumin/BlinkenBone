#!/bin/sh

echo "*** Generate deployment directories"
PROJECTDIR=~/retrocmp/blinkenbone/projects
DEPLOYROOT=$PROJECTDIR/11pidp8/deploy
ARCHIVENAME=blinkenlightd4rpi

chmod a+x $DEPLOYROOT/*/*.sh

echo "*** Copy precompiles win32 version of client"
cp $PROJECTDIR/07.2_blinkenlight_test/msvc/Debug/blinkenlight_test.exe $DEPLOYROOT/win32
cp $PROJECTDIR/3rdparty/oncrpc_win32/win32/librpc/lib/Debug/oncrpc.dll $DEPLOYROOT/win32
(
echo "*** make and copy Linux version of clients (x86 and raspberry)"
cd $PROJECTDIR/07.2_blinkenlight_test
export MAKE_TARGET_ARCH=X86
make all
cp bin-ubuntu-x86/blinkenlightapitst $DEPLOYROOT/ubuntu-x86

export MAKE_TARGET_ARCH=X64
make all
cp bin-ubuntu-x64/blinkenlightapitst $DEPLOYROOT/ubuntu-x64

export MAKE_TARGET_ARCH=RPI
make all
cp bin-rpi/blinkenlightapitst $DEPLOYROOT/rpi
)


(
echo "*** make and copy Raspberry version of server"
cd $PROJECTDIR/11pidp8/src
make all
cp bin-rpi/blinkenlightd $DEPLOYROOT/rpi
)

echo "*** Make archive $ARCHIVENAME"
# cleanup *.bak
find deploy -name \*.bak | xargs rm

tar cfvz  $ARCHIVENAME.tgz -C deploy .
