#
#!/bin/bash
#
# install SimH Binaries
# into root ~/pdp11,../vax,../pdp10


# in /tmp, BOTH on host and BeagleBone
REMOTEINSTALL_SCRIPT=/tmp/remoteinstall.sh

# libpcap and libnl must be these ourdated versions to work with SimH
LIBNL1_IPK=libnl1_1.1-r5.0.9_armv7a.ipk
LIBPCAP_IPK=libpcap_1.1.1-r1.9_armv7a.ipk

cat <<!!! >$REMOTEINSTALL_SCRIPT

echo "Install lipcap..."
opkg install /tmp/$LIBNL1_IPK
opkg install /tmp/$LIBPCAP_IPK

echo "Give existing lib installed by package 'libnl1' the expected name"
rm -f /usr/lib/libnl.so.1
ln /usr/lib/libnl.so.3 /usr/lib/libnl.so.1
# Thats it !!!
# (not tried) try to fix this in /etc/ld.so.conf
# then "sudo /sbin/ldconfig -v"


mkdir ~/pdp10
mkdir ~/pdp11
mkdir ~/vax

!!!

ssh ${REMOTEINSTALL_REMOTE_SSH_USERHOST} "date -u `date -u +%m%d%H%M%Y.%S`; echo `cat /etc/timezone` > /etc/timezone"

scp ${LIBNL1_IPK} ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:/tmp/${LIBNL1_IPK}
scp ${LIBPCAP_IPK} ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:/tmp/${LIBPCAP_IPK}
echo "Copy and Excute libpcap installer ..."
scp $REMOTEINSTALL_SCRIPT ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:$REMOTEINSTALL_SCRIPT

ssh ${REMOTEINSTALL_REMOTE_SSH_USERHOST}  "chmod +x $REMOTEINSTALL_SCRIPT ; /bin/sh $REMOTEINSTALL_SCRIPT"
(
echo "Build and Copy SimH ..."

# signal to make system: BeagleBone!
export MAKE_TARGET_ARCH=BEAGLEBONE

make -C src clean

make -C src pdp10
scp bin-beaglebone/pdp10 ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:pdp10/pdp10

make -C src pdp11
scp bin-beaglebone/pdp11 ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:pdp11/pdp11

make -C src vax
scp bin-beaglebone/vax ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:vax/vax

)
