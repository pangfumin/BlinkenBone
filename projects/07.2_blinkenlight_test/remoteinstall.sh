#!/bin/bash
#
# copies the blinkenlightapitst to beaglebone


### 0) build it
export MAKE_TARGET_ARCH=BEAGLEBONE
make clean
make blinkenlightapitst

scp bin-beaglebone/blinkenlightapitst ${REMOTEINSTALL_REMOTE_SSH_USERHOST}:

echo "... done"





