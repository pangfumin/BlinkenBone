#!/bin/sh
echo "*** Starting Blinkenlight API Server and test client on Raspberry PI"

echo "*** Start portmapper for RPC service ... fails if already running"
sudo rpcbind &
sleep 2

echo "*** Kill eventually running instances of Blinkenlight server ... only one allowed !"
sudo kill `pidof blinkenlightd`
sleep 1
echo "*** Start new instance of Blinkenlight server"
chmod a+x blinkenlightd
sudo ./blinkenlightd &
sleep 5
chmod a+x blinkenlightapitst
./blinkenlightapitst localhost
