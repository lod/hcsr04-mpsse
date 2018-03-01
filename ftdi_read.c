#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>

#include <libftdi1/ftdi.h>

/* Wiring:
 *  VCC - VCC (red)
 *  Trig - GPIOL0 (gray)
 *  Echo - TDI (green)
 *  GND - GND (black)
 */

/* Useful references:
 *   http://www.ftdichip.com/Support/Documents/AppNotes/AN_108_Command_Processor_for_MPSSE_and_MCU_Host_Bus_Emulation_Modes.pdf
 *   https://www.intra2net.com/en/developer/libftdi/documentation/group__libftdi.html
 *   https://www.intra2net.com/en/developer/libftdi/documentation/ftdi_8h_source.html
 */

unsigned int elapsed_ns(const struct timespec *start) {
	struct timespec current;
	clock_gettime(CLOCK_MONOTONIC, &current);
	return (current.tv_sec - start->tv_sec)*1e9 + (current.tv_nsec - start->tv_nsec);
}

void sample(struct ftdi_context *ftdi) {
	/* The HC-SR04 has a trigger input line and a pulse length output.
	 * A read is triggered by a 10us high pulse on the trigger line.
	 * The value is returned as a variable width pulse on the echo line.
	 */

	uint8_t buf[3]; // All the MPSSE commands we need are three bytes


	// SET_BITS_LOW configures the first 8 GPIOs (low bank)
	// 1: value bit field
	// 2: direction bit field, high = output
	buf[0] = SET_BITS_LOW;
	buf[1] = 0xFF; // drive outputs high
	buf[2] = 0x10; // GPIOL0 -> output, all others -> input
	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		fprintf(stderr, "Error: Sending gpio high command\n");
		return;
	}

	struct timespec burst = {0,10000}; // 10us
	nanosleep(&burst, NULL);

	buf[1] = 0; // drive outputs low
	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		fprintf(stderr, "Error: Sending gpio low command\n");
		return;
	}


	/* MPSSE clock data bytes in on -ve clock edge, LSB first
	 * This is a multibyte JTAG read command
	 * Every clock edge the FTDI chip will read the value of
	 * the echo line and store it as a bit.
	 * We trigger the maximum number of reads to get the biggest window.
	 * 1: LengthL, 2: LengthH (Length is 1 offset)
	 */
#define LEN 0x10000
	buf[0] = 0x2C;
	buf[1] = (LEN-1)&0xFF; // LengthL
	buf[2] = (LEN-1)>>8; // LengthH
	if (ftdi_write_data(ftdi, buf, 3) != 3) {
		fprintf(stderr, "Error: Sending initiate read command\n");
		return;
	}

	uint8_t out[LEN] = {0};

	// read doesn't wait, returns up to requested length
	int sofar = 0;
	struct timespec ts_start;
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	do {
		sofar += ftdi_read_data(ftdi, out+sofar, LEN-sofar);
	} while (sofar < LEN && elapsed_ns(&ts_start) < 1e9);
	if (sofar < LEN) {
		fprintf(stderr, "Error: timeout on read - probably far distance\n");
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
		fprintf(stderr, "Error: No high bits\n");
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
		fprintf(stderr, "Error: No pulse end\n");
		return;
	}


	// We default to a 6MHz sample frequency
	// 425162 samps 6M samps/s = 0.07s
	// HC-SR04 gives the following formula for air: cm = us/58
	// 100m = 0.001s/58
	// m = us/5800
	//   = u(samps / 6M)/5800
	//   = samps / 34800 (m)
	// printf("Dist %f\n", (float)(end-start)/34800);
	// Seems to be about 90% of measured value, from end of can
	
	// Using fundamentals, again from the datasheet:
	// range = high level time * velocity (340M/S) / 2;
	// m = s*340/2 = s*170
	// m = samps/6M*170
	// m = samps *0.000028333 = samps / 35294
	//printf("Dist %f\n", (float)(end-start)/6e6*340/2);
	// Again, seems to be about 90% of measured value

	// Hand corrected factor, tested accurate over 10-30cm range
	printf("Dist %f\n", (double)(end-start)/31750);
}


static int run = 1;

void intHandler(int ignored) {
	run = 0;
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

	if (ftdi_usb_reset(ftdi) < 0) goto configure_error;
	if (ftdi_set_interface(ftdi, INTERFACE_A) < 0) goto configure_error;
	if (ftdi_set_latency_timer(ftdi, 1) < 0) goto configure_error;
	if (ftdi_set_bitmode(ftdi, 0xfb, BITMODE_MPSSE) < 0) goto configure_error;

	// Set up interrupt handler, so we close nicely on a Ctrl-C event
	struct sigaction act;
	act.sa_handler = intHandler;
	sigaction(SIGINT, &act, NULL);

	while(run) {
		sample(ftdi);
	}

	ftdi_usb_close(ftdi);
	ftdi_free(ftdi);

	return 0;

configure_error:
	fprintf(stderr, "unable to configure ftdi device: %s\n", ftdi_get_error_string(ftdi));
	ftdi_usb_close(ftdi);

ftdi_error:
	ftdi_free(ftdi);

error:
	return 1;
}
