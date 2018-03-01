#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#include <libftdi1/ftdi.h>

#define LEN 0xFFFF

/* Wiring:
 *  VCC - VCC (red)
 *  Trig - GPIOL0 (gray)
 *  Echo - TDI (green)
 *  GND - GND (black)
 */

unsigned int elapsed_ns(const struct timespec *start, const struct timespec *end) {
	return (end->tv_sec - start->tv_sec)*1e9 + (end->tv_nsec - start->tv_nsec);
}

void sample(struct ftdi_context *ftdi) {
	char buf[3];
	// SET_BITS_LOW configures the first 8 lines <value> <direction>
	buf[0] = SET_BITS_LOW;
	buf[1] = 0xFF; // drive outputs high
	buf[2] = 0x10; // GPIOL0 == output, all others = input
	int ret;
	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		printf("Error: Sending gpio high command\n");
		return;
	}

	struct timespec burst = {0,10000}; // 10us
	nanosleep(&burst, NULL);

	buf[1] = 0; // drive outputs low
	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		printf("Error: Sending gpio low command\n");
		return;
	}

	buf[0] = 0x2C; // MPSSE_DO_READ | ??
	buf[1] = (LEN-1) & 0xFF;
	buf[2] = (LEN-1) >> 8;

	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		printf("Error: Sending initiate read command\n");
		return;
	}

	uint8_t out[LEN] = {0};

	// read doesn't wait, returns up to requested length
	int sofar = 0;
	struct timespec ts_start, ts_current;
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	do {
		ret = ftdi_read_data(ftdi, out+sofar, LEN-sofar);
		sofar += ret;
		clock_gettime(CLOCK_MONOTONIC, &ts_current);
		// printf("rread %d %d %d\n", ret, sofar, elapsed_ns(&ts_start, &ts_current));
	} while (sofar < LEN && elapsed_ns(&ts_start, &ts_current) < 1e9);
	if (sofar < LEN) {
		printf("Error: timeout on read - probably far distance\n");
		return;
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
		return;
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
		return;
	}

	printf("Dist %f\n", (float)(end-start)/34800);

	// We default to a 6MHz sample frequency
	// 425162 samps 6M samps/s = 0.07s
	// HC-SR04 gives the following formula for air: cm = us/58
	// 100m = 0.001s/58
	// samps / 6M = ?s
	// m = us/5800
	//   = u(samps / 6M)/5800
	//   = samps / 34800 (m)
	//
	//   TODO: Values seem to be about 90% of real
}

static int terminate = 0;

void intHandler(int whocares) {
	terminate = 1;
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


	struct sigaction act;
	act.sa_handler = intHandler;
	sigaction(SIGINT, &act, NULL);

	while(!terminate) {
		sample(ftdi);
	}



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
