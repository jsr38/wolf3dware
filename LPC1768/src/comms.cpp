#include "mbed.h"
#include "rtos.h"

#include "USBSerial.h"

#include <stdbool.h>
#include <string.h>

// usb CDC serial
static USBSerial *usb_serial= nullptr;

// reads lines from CDC and dispatched full lines to parser
static Thread *CDCThreadHandle= nullptr;

static void cdcThread(void const *argument);

extern bool commandLineHandler(const char*);
extern void kickQueue();
extern void setLed(int, bool);

static void serialReceived()
{
	if(CDCThreadHandle != nullptr){
		CDCThreadHandle->signal_set(0x01);
	}
}

static void serialConnected(bool connected)
{
	setLed(3, connected);
	if(connected){
		CDCThreadHandle->signal_set(0x02);
	}else{
		setLed(2, 0);
	}
}

int commsSetup(void)
{
	usb_serial= new USBSerial(0x1f00, 0x2012, 0x0001, false);

	// setup USB CDC
	usb_serial->attach(serialReceived);
	usb_serial->attach(serialConnected);
	// start communication threads
	CDCThreadHandle = new Thread(cdcThread, nullptr,  osPriorityNormal, 1024*2);

	return 1;
}

// called to send replies back to USB Serial
// Note must not exceed 64 bytes for USB
bool serial_reply(const char *buf, size_t len)
{
	return usb_serial->writeBlock((uint8_t*)buf, len);
}

#define MAXLINELEN 132
static char line[MAXLINELEN];
static uint16_t cnt = 0;

static void cdcThread(void const *argument)
{
	bool toggle= false;
	uint8_t c;
	for (;;) {

		// TODO handle timeout and kick queue every now and then
		osEvent ev= Thread::signal_wait(0);
		if(ev.status == osEventSignal) {
			if(ev.value.signals & 0x01) {
				setLed(2, toggle);
				toggle= !toggle;
			}
			if(ev.value.signals & 0x02) {
				usb_serial->puts("Welcome to Wolf3DWare\r\nok\r\n");
			}

		}else{
			continue;
		}

		while(usb_serial->available() > 0) {
			c= usb_serial->_getc();

			if(c == '\n') {
				if(cnt == 0) continue; //ignore empty lines

				// dispatch on NL. This blocks if queue is full etc
				line[cnt]= 0;
				commandLineHandler(line);
				cnt= 0;

			}else if(c == '\r'){
				// ignore CR
				continue;

			}else if(c == 8 || c == 127) { // BS or DEL
				if(cnt > 0) --cnt;

			}else if(cnt >= sizeof(line)-1) {
				// discard the excess of long lines
				continue;

			}else{
				line[cnt++]= c;
			}
		}
	}
}