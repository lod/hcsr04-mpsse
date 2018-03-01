#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <libftdi1/ftdi.h>

void print_byte(uint8_t byte) {
	printf("%c %c %c %c %c %c %c %c\n",
			byte & 0x80 ? '1' : '0',
			byte & 0x40 ? '1' : '0',
			byte & 0x20 ? '1' : '0',
			byte & 0x10 ? '1' : '0',
			byte & 0x08 ? '1' : '0',
			byte & 0x04 ? '1' : '0',
			byte & 0x02 ? '1' : '0',
			byte & 0x01 ? '1' : '0'
	);
}

int main(void) {

	struct ftdi_context *ftdi;
	if ((ftdi = ftdi_new()) == 0) {
		fprintf(stderr, "ftdi_new failed\n");
		goto error;
	}

	// Only want a FT232HL/Q type device
	// To be precise, we only support the C232HM EDHSL-0 cable
	if (ftdi_usb_open(ftdi, 0x0403, 0x6014) < 0) {
		fprintf(stderr, "unable to open ftdi device: %s\n", ftdi_get_error_string(ftdi));
		goto ftdi_error;
	}

	ftdi_usb_reset(ftdi);
	ftdi_set_interface(ftdi, INTERFACE_A);
	ftdi_set_latency_timer(ftdi, 1);
	ftdi_set_bitmode(ftdi, 0xfb, BITMODE_MPSSE);

	printf("type %d\n", ftdi->type); // output to reassure that we are running

	/* Wiring:
	 *  VCC - VCC (red)
	 *  	Trig - TDO (yellow)
	 *  Trig - GPIOL0 (gray)
	 *  Echo - TDI (green)
	 *  GND - GND (black)
	 */

	char buf[3];
	// SET_BITS_LOW configures the first 8 lines <value> <direction>
	buf[0] = SET_BITS_LOW;
	buf[1] = 0xFF; // drive outputs high
	buf[2] = 0x10; // GPIOL0 == output, all others = input
	int ret;
	ret = ftdi_write_data(ftdi, buf, 3);
	printf("High %d\n", ret);

	struct timespec burst = {0,10000}; // 10us
	nanosleep(&burst, NULL);

	buf[1] = 0; // drive outputs low
	ret = ftdi_write_data(ftdi, buf, 3);
	printf("Low %d\n", ret);


	/*
	uint8_t val;
	for (int i = 0; i < 100; i++) {
		ftdi_read_pins(ftdi, &val); print_byte(val);
	}
	*/

#define LEN 0xFFFF
	//buf[0] = MPSSE_DO_READ;
	buf[0] = 0x2C;
	buf[1] = (LEN-1) & 0xFF;
	buf[2] = (LEN-1) >> 8;

	ret = ftdi_write_data(ftdi, buf, 3);
	printf("read %d\n", ret);

	uint8_t out[LEN] = {0};

	// read doesn't wait, returns up to requested length
	// TODO: timeout
	int sofar = 0;
	do {
		ret = ftdi_read_data(ftdi, out+sofar, LEN-sofar);
		sofar += ret;
		printf("rread %d %d\n", ret, sofar);
	} while (sofar < LEN);

	for (int i = 0; i < sofar; i++) {
		print_byte(out[i]);
	}

	// Count the number of high bits, this is our pulse length
	int start = 0, end = LEN;
	for (int i = 0; i < LEN; i++) {
		if (out[i] != 0) {
			start = i*8;
			uint8_t startb = out[i];
			while (startb >>= 1) start++;
			break;
		}
	}
	if (!start) {
		printf("Error: No high bits\n");
		goto open_error;
	}

	for (int i = LEN; i; i--) {
		if (out[i] != 0x00) {
			end = i*8;
			uint8_t endb = out[i];
			while (endb >>= 1) end++;
			break;
		}
	}
	if (end == LEN) {
		printf("Error: No pulse end\n");
		goto open_error;
	}

	printf("PULSE %d-%d = %d\n", start, end, end-start);
	printf("Dist %f", (float)(end-start)/34800);

	// We default to a 6MHz sample frequency
	// 425162 samps 6M samps/s = 0.07s
	// HC-SR04 gives the following formula for air: cm = us/58
	// 100m = 0.001s/58
	// samps / 6M = ?s
	// m = us/5800
	//   = u(samps / 6M)/5800
	//   = samps / 34800 (m)


	//printf("Pausing\n");
	//getchar();


	ftdi_usb_close(ftdi);
	ftdi_free(ftdi);

	return 0;

open_error:
	ftdi_usb_close(ftdi);

ftdi_error:
	ftdi_free(ftdi);

error:
	return 1;
}
