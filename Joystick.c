/*
Nintendo Switch Fightstick - Proof-of-Concept

Based on the LUFA library's Low-Level Joystick Demo
	(C) Dean Camera
Based on the HORI's Pokken Tournament Pro Pad design
	(C) HORI

This project implements a modified version of HORI's Pokken Tournament Pro Pad
USB descriptors to allow for the creation of custom controllers for the
Nintendo Switch. This also works to a limited degree on the PS3.

Since System Update v3.0.0, the Nintendo Switch recognizes the Pokken
Tournament Pro Pad as a Pro Controller. Physical design limitations prevent
the Pokken Controller from functioning at the same level as the Pro
Controller. However, by default most of the descriptors are there, with the
exception of Home and Capture. Descriptor modification allows us to unlock
these buttons for our use.
*/

/** \file
 *
 *  Main source file for the posts printer demo. This file contains the main tasks of
 *  the demo and is responsible for the initial application hardware configuration.
 */

#include "Joystick.h"

#define TX_LED 0b00100000
#define RX_LED 0b00010000
#define Reset_Print 0b00001000
#define Oscilloscope_A 0b00000100
#define Oscilloscope_B 0b00000010

extern const uint8_t image_data[0x12c1] PROGMEM;

// Main entry point.
int main(void)
{
	// We'll start by performing hardware and peripheral setup.
	SetupHardware();
	// We'll then enable global interrupts for our use.
	GlobalInterruptEnable();

	PORTB = PORTB | Oscilloscope_A;

	// Once that's done, we'll enter an infinite loop.
	for (;;)
	{
		// We need to run our task to process and deliver data for our IN and OUT endpoints.
		HID_Task();
		// We also need to run the main USB management task.
		USB_USBTask();
	}
}

// Configures hardware and peripherals, such as the USB peripherals.
void SetupHardware(void)
{
	// We need to disable watchdog if enabled by bootloader/fuses.
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// We need to disable clock division before initializing the USB hardware.
	clock_prescale_set(clock_div_1);

	// We can then initialize our hardware and peripherals, including the USB stack.
	DDRD = TX_LED | RX_LED;
	PORTD = 0xFF;
	DDRB = Oscilloscope_A | Oscilloscope_B;
	PORTB = 0x00;

	// The USB stack should be initialized last.
	USB_Init();
}

// Fired to indicate that the device is enumerating.
void EVENT_USB_Device_Connect(void)
{
	// We can indicate that we're enumerating here (via status LEDs, sound, etc.).
}

// Fired to indicate that the device is no longer connected to a host.
void EVENT_USB_Device_Disconnect(void)
{
	// We can indicate that our device is not ready (via status LEDs, sound, etc.).
}

// Fired when the host set the current configuration of the USB device after enumeration.
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	// We setup the HID report endpoints.
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_IN_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);

	// We can read ConfigSuccess to indicate a success or failure at this point.
}

// Process control requests sent to the device from the USB host.
void EVENT_USB_Device_ControlRequest(void)
{
	// We can handle two control requests: a GetReport and a SetReport.

	// Not used here, it looks like we don't receive control request from the Switch.
}

// Process and deliver data from IN and OUT endpoints.
void HID_Task(void)
{
	// If the device isn't connected and properly configured, we can't do anything here.
	if (USB_DeviceState != DEVICE_STATE_Configured)
		return;

	// We'll start with the OUT endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);
	// We'll check to see if we received something on the OUT endpoint.
	if (Endpoint_IsOUTReceived())
	{
		// If we did, and the packet has data, we'll react to it.
		if (Endpoint_IsReadWriteAllowed())
		{
			// We'll create a place to store our data received from the host.
			USB_JoystickReport_Output_t JoystickOutputData;
			// We'll then take in that data, setting it up in our storage.
			while(Endpoint_Read_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData), NULL) != ENDPOINT_RWSTREAM_NoError);
			// At this point, we can react to this data.

			// However, since we're not doing anything with this data, we abandon it.
		}
		// Regardless of whether we reacted to the data, we acknowledge an OUT packet on this endpoint.
		Endpoint_ClearOUT();

		PORTB = (~PORTB & Oscilloscope_A) | (PORTB & ~Oscilloscope_A);
	}

	// We'll then move on to the IN endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_IN_EPADDR);
	// We first check to see if the host is ready to accept data.
	if (Endpoint_IsINReady())
	{
		// We'll create an empty report.
		USB_JoystickReport_Input_t JoystickInputData;
		// We'll then populate this report with what we want to send to the host.
		GetNextReport(&JoystickInputData);
		// Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
		while(Endpoint_Write_Stream_LE(&JoystickInputData, sizeof(JoystickInputData), NULL) != ENDPOINT_RWSTREAM_NoError);
		// We then send an IN packet on this endpoint.
		Endpoint_ClearIN();

		PORTB = (~PORTB & Oscilloscope_B) | (PORTB & ~Oscilloscope_B);
	}
}

// Sync the USB report stream to 30 fps, and enable the blanks skipping
// #define SYNC_TO_30_FPS
// #define SKIP_BLANKS

// Repeat ECHOES times the last sent report.

#define ECHOES 3

// Printer internal state
typedef enum {
	SYNC_CONTROLLER,
	SYNC_POSITION,
	STOP_X,
	STOP_Y,
	MOVE_X,
	MOVE_Y,
	DONE
} State_t;
State_t state = SYNC_CONTROLLER;

USB_JoystickReport_Input_t last_report;
int echoes = 0;

int command_count = 0;

int xpos = 0;
int ypos = 0;

#define max(a, b) (a > b ? a : b)
#define ms_2_count(ms) (ms / (ECHOES + 1) / (max(POLLING_MS, 8) / 8 * 8))
#define is_black(x, y) (pgm_read_byte(&(image_data[((x) / 8) + ((y) * 40)])) & 1 << ((x) % 8))

// Prepare the next report for the host.
void GetNextReport(USB_JoystickReport_Input_t* const ReportData)
{

	// Repeat ECHOES times the last report.
	if (echoes > 0)
	{
		memcpy(ReportData, &last_report, sizeof(USB_JoystickReport_Input_t));
		echoes--;
		return;
	}

	// Prepare an empty report.
	memset(ReportData, 0, sizeof(USB_JoystickReport_Input_t));
	ReportData->LX = STICK_CENTER;
	ReportData->LY = STICK_CENTER;
	ReportData->RX = STICK_CENTER;
	ReportData->RY = STICK_CENTER;
	ReportData->HAT = HAT_CENTER;

	// States and moves management.
	switch (state)
	{
		case SYNC_CONTROLLER:
			if (command_count > ms_2_count(2000))
			{
				command_count = 0;
				state = SYNC_POSITION;
			}
			else
			{
				if (command_count == ms_2_count(500) || command_count == ms_2_count(1000))
				{
					PORTD = (~PORTD & TX_LED) | (PORTD & ~TX_LED);
					ReportData->Button |= SWITCH_L | SWITCH_R;
				}
				else if (command_count == ms_2_count(1500) || command_count == ms_2_count(2000))
				{
					PORTD = (~PORTD & TX_LED) | (PORTD & ~TX_LED);
					ReportData->Button |= SWITCH_A;
				}
				else
				{
					PORTD = PORTD | TX_LED;
				}
				command_count++;
			}
			break;
		case SYNC_POSITION:
			if (command_count == ms_2_count(4000))
			{
				command_count = 0;
				xpos = 0;
				ypos = 0;
				state = STOP_X;
			}
			else
			{
				// Moving faster with LX/LY.
				ReportData->LX = STICK_MIN;
				ReportData->LY = STICK_MIN;
				// Clear the screen.
				if (command_count == ms_2_count(1500) || command_count == ms_2_count(3000))
				{
					PORTD = (~PORTD & TX_LED) | (PORTD & ~TX_LED);
					ReportData->Button |= SWITCH_LCLICK;
				}
				else
				{
					PORTD = PORTD | TX_LED;
				}
				command_count++;
			}
			break;
		case STOP_X:
			state = MOVE_X;
			break;
		case STOP_Y:
			if (ypos < 120 - 1)
				state = MOVE_Y;
			else
				state = DONE;
			break;
		case MOVE_X:
			PORTD = (~PORTD & TX_LED) | (PORTD & ~TX_LED);
			if (ypos % 2)
			{
				ReportData->HAT = HAT_LEFT;
				xpos--;
			}
			else
			{
				ReportData->HAT = HAT_RIGHT;
				xpos++;
			}
			if (xpos > 0 && xpos < 320 - 1)
				state = STOP_X;
			else
				state = STOP_Y;
			break;
		case MOVE_Y:
			PORTD = (~PORTD & TX_LED) | (PORTD & ~TX_LED);
			ReportData->HAT = HAT_BOTTOM;
			ypos++;
			state = STOP_X;
			break;
		case DONE:
			return;
	}

	// Inking
	if (state != SYNC_CONTROLLER && state != SYNC_POSITION)
		if (is_black(xpos, ypos))
			ReportData->Button |= SWITCH_A;

	// Prepare to echo this report.
	memcpy(&last_report, ReportData, sizeof(USB_JoystickReport_Input_t));
	echoes = ECHOES;
}
