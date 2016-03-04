PidP8 von Oascar Vermeulen:
ein PDP8-SimH, der die GPIOs eines RaspBerry PI ansteuert.
Angeschlossen ist per Multiplex eine imitiertes PDP/I panel.


Panel Controls



Aufbau:
 gpio.c: enthält Oscars MUX und gPIO logic.
 eigene endlos loop, läuft in eigenem thread.
 KOmmunikation über globals variable.
    uint32 switchstatus[3] = { 0 }; // bitfields: 3 rows of up to 12 switches
    uint32 ledstatus[8] = { 0 };	// bitfields: 8 ledrows of up to 12 LEDs

Fahrplan:
1) eigener BlinkenBoneServer
- Startet gpio thread
- implementiert als raw "Controls" switchstatus 0..2
  und ledstatus 0..7
2)
 implementiere PDP8/I Controls.


