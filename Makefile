all:
	gcc -Wall -std=gnu11 -o ftdi_read -lftdi1 ftdi_read.c
