# MPSSE HC-SR04

This simple project allows the connection of a HC-SR04 ultrasonic range sensor to a PC using an FTDI MPSSE cable.

It was developed because I was too lazy to pull out a microcontroller and decided to just offload the work to an FTDI chip.

We use the advanced MPSSE functionality for all the timing so we don't have to pretend we can do precise timing on a PC.

## To use

Hardware requirements:
	FTDI C232HM EDHSL-0 cable
	HC-SR04 compatible sensor module

Wire as followed:
	HC-SR04 - EDHSL
	VCC     - VCC (red)
	Trig    - GPIOL0 (gray)
	Echo    - TDI (green)
	GND     - GND (black)

Build requirements:
	libftdi (libftdi1 libftdi1-dev)
	standard C compilation environment

Note, this was developed and tested on a Linux system. All the system calls are POSIX standard but some tweaking may be required.

To compile: `make`
To run: `./ftdi_read`
To exit: Ctrl-C

The program outputs the current distance in meters about six times a second.

The MPSSE portions of the program are heavily commented as I documented my learnings as I went. This makes the program a potentially useful example of MPSSE setting GPIOs and performing reads, it is considerably simpler than other examples I found.
