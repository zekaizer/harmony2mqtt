//============================================================================
// Name        : HarmonyInput.cpp
// Author      : Watea
// Version     :
//============================================================================

#include <linux/hid.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <iostream>

// HARMONY USB interface, thanks to USB sniffer
#define HARMONY_CONFIG_INDEX        0
#define HARMONY_INTERFACE_INDEX     2
#define HARMONY_ALT_SETTING_INDEX   0
#define HARMONY_ENDPOINT_INDEX      0
#define HARMONY_VENDOR_ID           0x046d
#define HARMONY_PRODUCT_ID          0xc52b
#define HARMONY_TIMEOUT             1000
#define HARMONY_MAX_MSG_LENGTH      100
#define HARMONY_KEY_POSITION        3 // Found by sniffing

int mqtt_init(void);
int mqtt_connect(void);
int mqtt_send(const char *topic, const char *data, unsigned int len);

static inline unsigned long timespec_diff_ms(struct timespec *a, struct timespec *b) {
	long a_ms = (a->tv_sec * 1000ULL) + (a->tv_nsec / 1000000L);
	long b_ms = (b->tv_sec * 1000ULL) + (b->tv_nsec / 1000000L);
	return a_ms - b_ms;
}

void outputFrame(unsigned char* _frame, int _size) {
	std::cout << std::hex;
	std::cout << "Frame => ";
	for (int i = 0; i < _size; i++)
		std::cout << (unsigned short) _frame[i] << "/";
	std::cout << std::endl;
	std::cout << std::dec;
}
void outputKey(unsigned char* _frame, int _size) {
	std::cout << std::hex;
	std::cout << "KEY => " << (unsigned short) (_frame[HARMONY_KEY_POSITION] << 8) + _frame[HARMONY_KEY_POSITION + 1] << std::endl;
	std::cout << std::dec;
}

int main() {
	libusb_context *ctx;
	struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;

	std::cout << "Starting keys sniffer..." << std::endl;

	libusb_init(&ctx);
	libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL);

	// Look at the keyboard based on vendor and device id
	libusb_device_handle *device_handle = libusb_open_device_with_vid_pid(ctx, HARMONY_VENDOR_ID, HARMONY_PRODUCT_ID);

	std::cout << std::hex;
	std::cout << "Found Harmony Device: " << device_handle << std::endl << std::endl;

	// Get interface
	libusb_device *device = libusb_get_device(device_handle);
	libusb_get_device_descriptor(device, &desc);
	libusb_get_config_descriptor(device, HARMONY_CONFIG_INDEX, &config);
	const struct libusb_interface *iface = &config->interface[HARMONY_INTERFACE_INDEX];
	const struct libusb_interface_descriptor *iface_desc = &iface->altsetting[HARMONY_ALT_SETTING_INDEX];

	// Detach & claim interface from kernel driver
	libusb_detach_kernel_driver(device_handle, HARMONY_INTERFACE_INDEX);
	libusb_claim_interface(device_handle, HARMONY_INTERFACE_INDEX);

	// Bench for reading HARMONY Keys (Button Press)
	unsigned char data[HARMONY_MAX_MSG_LENGTH];
	int actual_length = 0;
	int index = 0;

	mqtt_init();
	mqtt_connect();

	struct event_prog {
		int code;
		unsigned long duration_ms;
	};

	struct event_prog evtps[4] = {0,};

	do {
 		libusb_interrupt_transfer(
			device_handle,
			iface_desc->endpoint[HARMONY_ENDPOINT_INDEX].bEndpointAddress,
			data,
			sizeof(data),
			&actual_length,
			HARMONY_TIMEOUT);

		// Key Value received (Key Release ignored here)
		// Note: HARMONY REMOTE seems to send over data; some false positive happen, to ignore in this bench
		if ((actual_length > 0) &&
			(data[HARMONY_KEY_POSITION] + data[HARMONY_KEY_POSITION + 1] + 1 != 0)) {

			/* get monotonic clock time */
			static struct timespec privtime;
			struct timespec curtime;
			unsigned long diff;

			clock_gettime(CLOCK_MONOTONIC, &curtime);
			diff = timespec_diff_ms(&curtime, &privtime);

			std::cout << std::endl;
			std::cout << "Time " << diff << "ms" << ", Index:" << index++ << ", Length:" << actual_length << std::endl;
			// Key Frame
			outputFrame(data, actual_length);

			privtime = curtime;

			if ((actual_length == 15) && data[0] == 0x20 && data[1] == 0x01) {
				int codes[4] = {0,};

				if (data[2] == 0x01) {
					for (int i = 0; i < 4; i++) {
						if (data[4 + i] > 0)
							codes[i] = data[4 + i];
					}
				} else if (data[2] == 0x03) {
					for (int i = 0; i < 4; i += 2) {
						if (data[3 + i] > 0 || data[4 + i] > 0)
							codes[i] = (data[3 + i] << 8) + data[4 + i];
					}
				}


				char buf[128 + 1] = {0,};
				char *pbuf = buf;

				for (int i = 0; i < 4; i++)
					if (codes[i] > 0)
						pbuf += sprintf(pbuf, "%04x ", codes[i]);

				if (buf == pbuf)
					pbuf += sprintf(pbuf, "0000");
				else
					pbuf--;

				mqtt_send("HarmonyCompanionRemote/scancode", buf, (uintptr_t)pbuf - (uintptr_t)buf);

				bool hold[4] = {false, false, false, false};

				for (int j = 0; j < 4; j++) {
					if (evtps[j].code)
						evtps[j].duration_ms += diff;
					for (int k = 0; k < 4; k++)
						if (evtps[j].code) {
							if (evtps[j].code == codes[k]) {
								hold[j] = true;
								codes[k] = 0;
								std::cout << "hold " << evtps[j].code << std::endl;
								break;
							}
						}
				}

				for (int j = 0; j < 4; j++)
					if (evtps[j].code && !hold[j]) {
						pbuf = buf;
						pbuf += sprintf(pbuf, "%04x %04lums", evtps[j].code, evtps[j].duration_ms);
						mqtt_send("HarmonyCompanionRemote/event", buf, (uintptr_t)pbuf - (uintptr_t)buf);
						std::cout << "released " << evtps[j].code << std::endl;
						evtps[j].code = 0;
						evtps[j].duration_ms = 0;
					}

				for (int j = 0; j < 4; j++)
					if (codes[j]) {
						std::cout << "pressed " << codes[j] << std::endl;
						for (int k = 0; k < 4; k++) {
							if (!evtps[k].code) {
								evtps[k].code = codes[j];
								break;
							}
						}
					}
				
				for (int j = 0; j < 4; j++)
					std::cout << "dump " << evtps[j].code << " " << evtps[j].duration_ms << std::endl;
			}
		}
		// // Not a Key Value Frame
		// else
		// 	mqtt_send("HarmonyCompanionRemote/scancode", "00", 2);
	} while (true);

	// Leave a clean environment
	libusb_release_interface(device_handle, HARMONY_INTERFACE_INDEX);
	libusb_attach_kernel_driver(device_handle, HARMONY_INTERFACE_INDEX);
	libusb_close(device_handle);
	libusb_exit(ctx);

	return 0;
}
