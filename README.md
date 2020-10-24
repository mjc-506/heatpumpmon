# heatpumpmon
Software and hardware for an esp8266 based interface for a Grant ASHP.

See https://hackaday.io/project/170602-reverse-engineering-grant-ashp-remote-protocol for details.

PCB design also available at https://easyeda.com/mjc506/heatpump

This repository contains hardware and software for an esp8266 based module that connects to a Grant ASHP remote, in order to monitor the performance of the system. The hardware would be capable of adjusting settings on the heat pump, and/or setting CH and DHW demands with small changes (there are two spare outputs on the board) but there is currently no software provision for this.

The remote is powered from the ASHP unit (12V) and receives serial at 1200baud over the same lines, using a T6B70BFG, which passes serial data to a microcontroller in the remote. The remote displays time of day, outdoor air temperature, indoor air temperature (there is an onboard thermistor) and status icons (power, DH/CHW demand, pump, fan, compressor status) by default, and can also query and change settings, and query various performance parameters (temperatures, rpms etc). The ASHP can operate without the remote connected, and just requires DH and CHW demand signals to its own PCB if it is not fitted.

The module directly reads the serial output from the T6B70BFG, but emulates button presses on the remote (it has so far not been possible to add a second 'remote' to the system). Using this module requires a small number of hookup wires to be connected to four switches, Vcc, Ground and the serial RX line in the remote. (ie, you may void your warranty) It may be possible to make these connections without soldering.
