// 
// Author: Bradley A. Minch
// Organization: Franklin W. Olin College of Engineering
// Revision History: 
//     01/19/2006 - Added wait for initial SE0 condition to clear at the end
//                  of InitUSB().
//     01/13/2006 - Fixed problem with DATA OUT transfers (only worked properly
//                  with class requests) in ProcessSetupToken.  Added code to
//                  disable all EPs except EP0 on a valid SET_CONFIGURATION
//                  request.  Changed code to use BSTALL instead of EPSTALL for
//                  Request Error on EP0.  Changed CLEAR_FEATURE, SET_FEATURE,
//                  and GET_STATUS requests to use BSTALL instead of EPSTALL.  
//                  Eliminated initial for loop in main().
//     11/14/2005 - Initial port to C for the Microchip C18 compiler complete
//     06/22/2005 - Added code to disable all endpoints on USRTIF and to mask
//                  bits 0, 1, and 7 of USTAT on TRNIF in ServiceUSB.
//     04/21/2005 - Initial public release.
//
// ============================================================================
// 
// Peripheral Description:
// 
// This peripheral enumerates as a vendor-specific device. The main event loop 
// blinks an LED connected to RA1 on and off at about 2 Hz and the peripheral 
// responds to a pair of vendor-specific requests that turn on or off an LED
// connected to RA0.  The firmware is configured to use an external 4-MHz 
// crystal, to operate as a low-speed USB device, and to use the internal 
// pull-up resistor.
// 
// ============================================================================
//
// Software Licence Agreement:
// 
// THIS SOFTWARE IS PROVIDED IN AN "AS IS" CONDITION.  NO WARRANTIES, WHETHER 
// EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED 
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE APPLY 
// TO THIS SOFTWARE. THE AUTHOR SHALL NOT, UNDER ANY CIRCUMSTANCES, BE LIABLE 
// FOR SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
// 
#include <pic18fregs.h>
#include "usb_defs.h"


code char at __CONFIG1L config1l = 0xFF &	_USBPLL_CLOCK_SRC_FROM_96MHZ_PLL_2_1L&
						_CPUDIV__OSC1_OSC2_SRC___3__96MHZ_PLL_SRC___4__1L&
						_PLLDIV_NO_DIVIDE__4MHZ_INPUT__1L;
						
code char at __CONFIG1H config1h = 0xFF &	_OSC_XT__XT_PLL__USB_XT_1H&
						_FCMEN_OFF_1H&
						_IESO_OFF_1H;

code char at __CONFIG2L config2l = 0xFF &	_VREGEN_ON_2L&
						_PUT_OFF_2L&
						_BODEN_ON_2L&
						_BODENV_4_2V_2L;

code char at __CONFIG2H config2h = 0xFF &	_WDT_DISABLED_CONTROLLED_2H&
						_WDTPS_1_32768_2H;

code char at __CONFIG3H config3h = 0xFF &	_CCP2MUX_RB3_3H&
						_MCLRE_MCLR_ON_RE3_OFF_3H&
						_LPT1OSC_OFF_3H&
						_PBADEN_PORTB_4_0__CONFIGURED_AS_DIGITAL_I_O_ON_RESET_3H;

code char at __CONFIG4L config4l = 0xFF &	_STVR_ON_4L&
						_LVP_OFF_4L&
						_ENICPORT_OFF_4L&
						_ENHCPU_OFF_4L&
						_BACKBUG_OFF_4L;
						
code char at __CONFIG5L config5l = 0xFF;
code char at __CONFIG5H config5h = 0xFF;
code char at __CONFIG6L config6l = 0xFF;
code char at __CONFIG6H config6h = 0xFF;
code char at __CONFIG7L config7l = 0xFF;
code char at __CONFIG7H config7h = 0xFF;


#define	SET_RA0			0x01		// vendor-specific request to set RA0 to high
#define	CLR_RA0			0x02		// vendor-specific request to set RA0 to low

BUFDESC USB_buffer_desc;
unsigned char USB_buffer_data[8];
unsigned char USB_error_flags;
unsigned char USB_curr_config;
unsigned char USB_device_status;
unsigned char USB_dev_req;
unsigned char USB_address_pending;
code unsigned char *USB_desc_ptr;
unsigned char USB_bytes_left;
unsigned char USB_packet_length;
unsigned char USB_USTAT;
unsigned char USB_USWSTAT;

code const unsigned char Device[] = {
	0x12,	// bLength
	DEVICE,	// bDescriptorType
	0x10,	// bcdUSB (low byte)
	0x01,	// bcdUSB (high byte)
	0x00,	// bDeviceClass
	0x00,	// bDeviceSubClass
	0x00,	// bDeviceProtocol
	MAX_PACKET_SIZE,	// bMaxPacketSize
	0xD8,	// idVendor (low byte)
	0x04,	// idVendor (high byte)
	0x01,	// idProduct (low byte)
	0x00,	// idProduct (high byte)
	0x00,	// bcdDevice (low byte)
	0x00,	// bcdDevice (high byte)
	0x01,	// iManufacturer
	0x02,	// iProduct
	0x00,	// iSerialNumber (none)
	NUM_CONFIGURATIONS	// bNumConfigurations
};

code const unsigned char Configuration1[] = {
	0x09,	// bLength
	CONFIGURATION,	// bDescriptorType
	0x12,	// wTotalLength (low byte)
	0x00,	// wTotalLength (high byte)
	NUM_INTERFACES,	// bNumInterfaces
	0x01,	// bConfigurationValue
	0x00,	// iConfiguration (none)
	0xA0,	// bmAttributes
	0x32,	// bMaxPower (100 mA)
	0x09,	// bLength (Interface1 descriptor starts here)
	INTERFACE,	// bDescriptorType
	0x00,	// bInterfaceNumber
	0x00,	// bAlternateSetting
	0x00,	// bNumEndpoints (excluding EP0)
	0xFF,	// bInterfaceClass (vendor specific class code)
	0x00,	// bInterfaceSubClass
	0xFF,	// bInterfaceProtocol (vendor specific protocol used)
	0x00	// iInterface (none)
};

code const unsigned char String0[] = {
	0x04,	// bLength
	STRING,	// bDescriptorType
	0x09,	// wLANGID[0] (low byte)
	0x04	// wLANGID[0] (high byte)
};

code const unsigned char String1[] = {
	0x36,	// bLength
	STRING,	// bDescriptorType
	'M', 0x00, 'i', 0x00, 'c', 0x00, 'r', 0x00, 'o', 0x00, 'c', 0x00, 'h', 0x00, 'i', 0x00, 'p', 0x00, ' ', 0x00,
	'T', 0x00, 'e', 0x00, 'c', 0x00, 'h', 0x00, 'n', 0x00, 'o', 0x00, 'l', 0x00, 'o', 0x00, 'g', 0x00, 'y', 0x00, ',', 0x00, ' ', 0x00,
	'I', 0x00, 'n', 0x00, 'c', 0x00, '.', 0x00
};

code const unsigned char String2[] = {
	0x44,	// bLength
	STRING,	// bDescriptorType
	'E', 0x00, 'N', 0x00, 'G', 0x00, 'R', 0x00, ' ', 0x00, '2', 0x00, '2', 0x00, '1', 0x00, '0', 0x00, ' ', 0x00,
	'P', 0x00, 'I', 0x00, 'C', 0x00, '1', 0x00, '8', 0x00, 'F', 0x00, '2', 0x00, '4', 0x00, '5', 0x00, '5', 0x00, ' ', 0x00,
	'U', 0x00, 'S', 0x00, 'B', 0x00, ' ', 0x00,
	'F', 0x00, 'i', 0x00, 'r', 0x00, 'm', 0x00, 'w', 0x00, 'a', 0x00, 'r', 0x00, 'e', 0x00
};

void InitUSB(void) {
	(void)*EP0_OUT_buffer;
	(void)*EP0_IN_buffer;
	(void)*EP1_OUT_buffer;
	(void)*EP1_IN_buffer;
	(void)*EP2_OUT_buffer;
	(void)*EP2_IN_buffer;
	(void)*EP3_OUT_buffer;
	(void)*EP3_IN_buffer;
	(void)*EP4_OUT_buffer;
	(void)*EP4_IN_buffer;
	(void)*EP5_OUT_buffer;
	(void)*EP5_IN_buffer;
	(void)*EP6_OUT_buffer;
	(void)*EP6_IN_buffer;
	(void)*EP7_OUT_buffer;
	(void)*EP7_IN_buffer;
	(void)*EP8_OUT_buffer;
	(void)*EP8_IN_buffer;
	(void)*EP9_OUT_buffer;
	(void)*EP9_IN_buffer;
	(void)*EP10_OUT_buffer;
	(void)*EP10_IN_buffer;
	(void)*EP11_OUT_buffer;
	(void)*EP11_IN_buffer;
	(void)*EP12_OUT_buffer;
	(void)*EP12_IN_buffer;
	(void)*EP13_OUT_buffer;
	(void)*EP13_IN_buffer;
	(void)*EP14_OUT_buffer;
	(void)*EP14_IN_buffer;
	(void)*EP15_OUT_buffer;
	(void)*EP15_IN_buffer;
	UIE = 0x00;					// mask all USB interrupts
	UIR = 0x00;					// clear all USB interrupt flags
	UCFG = 0x10;				// configure USB for low-speed transfers and to use the on-chip transciever and pull-up resistor
	UCON = 0x08;				// enable the USB module and its supporting circuitry
	USB_curr_config = 0x00;
	USB_USWSTAT = 0x00;			// default to powered state
	USB_device_status = 0x01;
	USB_dev_req = NO_REQUEST;	// No device requests in process
	TRISB = 0x00;
	PORTB = 0xFF;
	while (UCONbits.SE0);		// wait for the first SE0 to end
}

/*
 * Clear all EP control registers except for EP0 to disable EP1-EP15
 */
static void disableUEP(void) {
	UEP1 = 0x00;
	UEP2 = 0x00;
	UEP3 = 0x00;
	UEP4 = 0x00;
	UEP5 = 0x00;
	UEP6 = 0x00;
	UEP7 = 0x00;
	UEP8 = 0x00;
	UEP9 = 0x00;
	UEP10 = 0x00;
	UEP11 = 0x00;
	UEP12 = 0x00;
	UEP13 = 0x00;
	UEP14 = 0x00;
	UEP15 = 0x00;
}

void ServiceUSB(void) {
	__data BUFDESC *buf_desc_ptr;

	if (UIRbits.UERRIF) {
		UEIR = 0x00;
	} else if (UIRbits.SOFIF) {
		UIRbits.SOFIF = 0;
	} else if (UIRbits.IDLEIF) {
		UIRbits.IDLEIF = 0;
		UCONbits.SUSPND = 1;
	} else if (UIRbits.ACTVIF) {
		UIRbits.ACTVIF = 0;
		UCONbits.SUSPND = 0;
	} else if (UIRbits.STALLIF) {
		UIRbits.STALLIF = 0;
	} else if (UIRbits.URSTIF) {
		USB_curr_config = 0x00;
		UIRbits.TRNIF = 0;		// clear TRNIF four times to clear out the USTAT FIFO
		UIRbits.TRNIF = 0;
		UIRbits.TRNIF = 0;
		UIRbits.TRNIF = 0;
		/* disable all endpoints before resetting buffers */
		UEP0 = 0x00;
		disableUEP();

		BD0O.bytecount = MAX_PACKET_SIZE;
		BD0O.address = EP0_OUT_buffer;	// EP0 OUT gets a buffer
		BD0O.status = 0x88;		// set UOWN bit (USB can write)
		BD0I.address = EP0_IN_buffer;	// EP0 IN gets a buffer
		BD0I.status = 0x08;		// clear UOWN bit (MCU can write)
		
		BD1O.bytecount = MAX_PACKET_SIZE;
		BD1O.address = EP0_OUT_buffer;	// EP1 OUT gets a buffer
		BD1O.status = 0x88;		// set UOWN bit (USB can write)
		BD1I.address = EP0_IN_buffer;	// EP1 IN gets a buffer
		BD1I.status = 0x08;		// clear UOWN bit (MCU can write)
		
		UADDR = 0x00;				// set USB Address to 0
		UIR = 0x00;				// clear all the USB interrupt flags
		UEP0 = ENDPT_CONTROL;	// EP0 is a control pipe and requires an ACK
		UEIE = 0xFF;			// enable all error interrupts
		USB_USWSTAT = DEFAULT_STATE;
		USB_device_status = 0x01;	// self powered, remote wakeup disabled
		
	} else if (UIRbits.TRNIF) {	
		buf_desc_ptr = (__data void *)((BD_BASE)+(USTAT&0x7C));	// mask out bits 0, 1, and 7 of USTAT for offset into the buffer descriptor table
		USB_buffer_desc.status = buf_desc_ptr->status;
		USB_buffer_desc.bytecount = buf_desc_ptr->bytecount;
		USB_buffer_desc.address = buf_desc_ptr->address;
		USB_USTAT = USTAT;		// save the USB status register
		UIRbits.TRNIF = 0;		// clear TRNIF interrupt flag
		USB_error_flags = 0x00;	// clear USB error flags
		switch (USB_buffer_desc.status&0x3C) {	// extract PID bits
		case TOKEN_SETUP:
			ProcessSetupToken();
			break;
		case TOKEN_IN:
			ProcessInToken();
			break;
		case TOKEN_OUT:
			ProcessOutToken();
		}
		if (USB_error_flags&0x01) {		// if there was a Request Error...
			BD0O.bytecount = MAX_PACKET_SIZE;	// ...get ready to receive the next Setup token...
			BD0I.status = 0x84;
			BD0O.status = 0x84;					// ...and issue a protocol stall on EP0
		}
	}
}

void ProcessSetupToken(void) {
	unsigned char n;
	PORTB |= 0x04;
	for (n = 0; n<8; n++) {
		USB_buffer_data[n] = USB_buffer_desc.address[n];
	}
	BD0O.bytecount = MAX_PACKET_SIZE;	// reset the EP0 OUT byte count
	BD0I.status = 0x08;			// return the EP0 IN buffer to us (dequeue any pending requests)			
	BD0O.status = (!(USB_buffer_data[bmRequestType]&0x80) && (USB_buffer_data[wLength] || USB_buffer_data[wLengthHigh])) ? 0xC8:0x88;	// set EP0 OUT UOWN back to USB and DATA0/DATA1 packet according to the request type
	UCONbits.PKTDIS = 0;			// assuming there is nothing to dequeue, clear the packet disable bit
	USB_dev_req = NO_REQUEST;		// clear the device request in process
	switch (USB_buffer_data[bmRequestType]&0x60) {	// extract request type bits
	case STANDARD:
		StandardRequests();
		break;
	case CLASS:
		ClassRequests();
		break;
	case VENDOR:
		VendorRequests();
		break;
	default:
		USB_error_flags |= 0x01;	// set Request Error Flag
	}
}

void StandardRequests(void) {
	unsigned char *UEP;
	unsigned char n;
	BUFDESC *buf_desc_ptr;

	switch (USB_buffer_data[bRequest]) {
	case GET_STATUS:
		switch (USB_buffer_data[bmRequestType]&0x1F) {	// extract request recipient bits
		case RECIPIENT_DEVICE:
			BD0I.address[0] = USB_device_status;
			BD0I.address[1] = 0x00;
			BD0I.bytecount = 0x02;
			BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
			break;
		case RECIPIENT_INTERFACE:
			switch (USB_USWSTAT) {
			case ADDRESS_STATE:
				USB_error_flags |= 0x01;	// set Request Error Flag
				break;
			case CONFIG_STATE:
				if (USB_buffer_data[wIndex]<NUM_INTERFACES) {
					BD0I.address[0] = 0x00;
					BD0I.address[1] = 0x00;
					BD0I.bytecount = 0x02;
					BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
				} else {
					USB_error_flags |= 0x01;	// set Request Error Flag
				}
			}
			break;
		case RECIPIENT_ENDPOINT:
			switch (USB_USWSTAT) {
			case ADDRESS_STATE:
				if (!(USB_buffer_data[wIndex]&0x0F)) {	// get EP, strip off direction bit and see if it is EP0
					BD0I.address[0] = (((USB_buffer_data[wIndex]&0x80) ? BD0I.status:BD0O.status)&0x04)>>2;	// return the BSTALL bit of EP0 IN or OUT, whichever was requested
					BD0I.address[1] = 0x00;
					BD0I.bytecount = 0x02;
					BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
				} else {
					USB_error_flags |= 0x01;	// set Request Error Flag
				}
				break;
			case CONFIG_STATE:
				UEP = (unsigned char *)&UEP0;
				n = USB_buffer_data[wIndex]&0x0F;	// get EP and strip off direction bit for offset from UEP0
				buf_desc_ptr = &BD0O+((n<<1)|((USB_buffer_data[wIndex]&0x80) ? 0x01:0x00));	// compute pointer to the buffer descriptor for the specified EP
				if (UEP[n]&((USB_buffer_data[wIndex]&0x80) ? 0x02:0x04)) { // if the specified EP is enabled for transfers in the specified direction...
					BD0I.address[0] = ((buf_desc_ptr->status)&0x04)>>2;	// ...return the BSTALL bit of the specified EP
					BD0I.address[1] = 0x00;
					BD0I.bytecount = 0x02;
					BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
				} else {
					USB_error_flags |= 0x01;	// set Request Error Flag
				}
				break;
			default:
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			break;
		default:
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case CLEAR_FEATURE:
	case SET_FEATURE:
		switch (USB_buffer_data[bmRequestType]&0x1F) {	// extract request recipient bits
		case RECIPIENT_DEVICE:
			switch (USB_buffer_data[wValue]) {
			case DEVICE_REMOTE_WAKEUP:
				if (USB_buffer_data[bRequest]==CLEAR_FEATURE)
					USB_device_status &= 0xFE;
				else
					USB_device_status |= 0x01;
				BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
				BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
				break;
			default:
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			break;
		case RECIPIENT_ENDPOINT:
			switch (USB_USWSTAT) {
			case ADDRESS_STATE:
				if (!(USB_buffer_data[wIndex]&0x0F)) {	// get EP, strip off direction bit, and see if its EP0
					BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
					BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
				} else {
					USB_error_flags |= 0x01;	// set Request Error Flag
				}
				break;
			case CONFIG_STATE:
				UEP = (unsigned char *)&UEP0;
				if (n = USB_buffer_data[wIndex]&0x0F) {	// get EP and strip off direction bit for offset from UEP0, if not EP0...
					buf_desc_ptr = &BD0O+((n<<1)|((USB_buffer_data[wIndex]&0x80) ? 0x01:0x00));	// compute pointer to the buffer descriptor for the specified EP
					if (USB_buffer_data[wIndex]&0x80) {	// if the specified EP direction is IN...
						if (UEP[n]&0x02) {	// if EPn is enabled for IN transfers...
							buf_desc_ptr->status = (USB_buffer_data[bRequest]==CLEAR_FEATURE) ? 0x00:0x84;
						} else {
							USB_error_flags |= 0x01;	// set Request Error Flag									
						}
					} else {	// ...otherwise the specified EP direction is OUT, so...
						if (UEP[n]&0x04) {	// if EPn is enabled for OUT transfers...
							buf_desc_ptr->status = (USB_buffer_data[bRequest]==CLEAR_FEATURE) ? 0x88:0x84;
						} else {
							USB_error_flags |= 0x01;	// set Request Error Flag									
						}
					}
				}
				if (!(USB_error_flags&0x01)) {	// if there was no Request Error...
					BD0I.bytecount = 0x00;
					BD0I.status = 0xC8;		// ...send packet as DATA1, set UOWN bit
				}
				break;
			default:
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			break;
		default:
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case SET_ADDRESS:
		if (USB_buffer_data[wValue]>0x7F) {	// if new device address is illegal, send Request Error
			USB_error_flags |= 0x01;	// set Request Error Flag
		} else {
			USB_dev_req = SET_ADDRESS;	// processing a SET_ADDRESS request
			USB_address_pending = USB_buffer_data[wValue];	// save new address
			BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
			BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		}
		break;
	case GET_DESCRIPTOR:
		USB_dev_req = GET_DESCRIPTOR;	// processing a GET_DESCRIPTOR request
		switch (USB_buffer_data[wValueHigh]) {
		case DEVICE:
			USB_desc_ptr = Device;
			USB_bytes_left = USB_desc_ptr[0];
			if ((USB_buffer_data[wLengthHigh]==0x00) && (USB_buffer_data[wLength]<USB_bytes_left)) {
				USB_bytes_left = USB_buffer_data[wLength];
			}
			SendDescriptorPacket();
			break;
		case CONFIGURATION:
			switch (USB_buffer_data[wValue]) {
			case 0:
				USB_desc_ptr = Configuration1;
				break;
			default:
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			if (!(USB_error_flags&0x01)) {
				USB_bytes_left = USB_desc_ptr[2];	// wTotalLength at an offset of 2
				if ((USB_buffer_data[wLengthHigh]==0x00) && (USB_buffer_data[wLength]<USB_bytes_left)) {
					USB_bytes_left = USB_buffer_data[wLength];
				}
				SendDescriptorPacket();
			}
			break;
		case STRING:
			switch (USB_buffer_data[wValue]) {
			case 0:
				USB_desc_ptr = String0;
				break;
			case 1:
				USB_desc_ptr = String1;
				break;
			case 2:
				USB_desc_ptr = String2;
				break;
			default:
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			if (!(USB_error_flags&0x01)) {
				USB_bytes_left = USB_desc_ptr[0];
				if ((USB_buffer_data[wLengthHigh]==0x00) && (USB_buffer_data[wLength]<USB_bytes_left)) {
					USB_bytes_left = USB_buffer_data[wLength];
				}
				SendDescriptorPacket();
			}
			break;
		default:
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case GET_CONFIGURATION:
		BD0I.address[0] = USB_curr_config;	// copy current device configuration to EP0 IN buffer
		BD0I.bytecount = 0x01;
		BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		break;
	case SET_CONFIGURATION:
		if (USB_buffer_data[wValue]<=NUM_CONFIGURATIONS) {
			disableUEP();
			switch (USB_curr_config = USB_buffer_data[wValue]) {
			case 0:
				USB_USWSTAT = ADDRESS_STATE;
				break;
			default:
				USB_USWSTAT = CONFIG_STATE;
			}
			BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
			BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		} else {
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case GET_INTERFACE:
		switch (USB_USWSTAT) {
		case CONFIG_STATE:
			if (USB_buffer_data[wIndex]<NUM_INTERFACES) {
				BD0I.address[0] = 0x00;	// always send back 0 for bAlternateSetting
				BD0I.bytecount = 0x01;
				BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
			} else {
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			break;
		default:
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case SET_INTERFACE:
		switch (USB_USWSTAT) {
		case CONFIG_STATE:
			if (USB_buffer_data[wIndex]<NUM_INTERFACES) {
				switch (USB_buffer_data[wValue]) {
				case 0:		// currently support only bAlternateSetting of 0
					BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
					BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
					break;
				default:
					USB_error_flags |= 0x01;	// set Request Error Flag
				}
			} else {
				USB_error_flags |= 0x01;	// set Request Error Flag
			}
			break;
		default:
			USB_error_flags |= 0x01;	// set Request Error Flag
		}
		break;
	case SET_DESCRIPTOR:
	case SYNCH_FRAME:
	default:
		USB_error_flags |= 0x01;	// set Request Error Flag
	}
}

void ClassRequests(void) {
	switch (USB_buffer_data[bRequest]) {
	default:
		USB_error_flags |= 0x01;	// set Request Error Flag
	}
}

void VendorRequests(void) {
	switch (USB_buffer_data[bRequest]) {
	case SET_RA0:
		PORTAbits.RA0 = 1;		// set RA0 high
		BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
		BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		break;
	case CLR_RA0:
		PORTAbits.RA0 = 0;		// set RA0 low
		BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
		BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		break;
	default:
		USB_error_flags |= 0x01;	// set Request Error Flag
	}
}

void ProcessInToken(void) {
	switch (USB_USTAT&0x78) {	// extract the EP bits
	case EP0:
		switch (USB_dev_req) {
		case SET_ADDRESS:
			switch (UADDR = USB_address_pending) {
			case 0:
				USB_USWSTAT = DEFAULT_STATE;
				break;
			default:
				USB_USWSTAT = ADDRESS_STATE;
			}
			break;
		case GET_DESCRIPTOR:
			SendDescriptorPacket();
			break;
		}
		break;
	case EP1:
		BD1I.address[0] = 0x55;
		BD1I.bytecount = 0x01;		// set EP0 IN byte count to 0
		BD1I.status = 0x80;		// send packet as DATA1, set UOWN bit
		break;
	case EP2:
	case EP3:
	case EP4:
	case EP5:
	case EP6:
	case EP7:
	case EP8:
	case EP9:
	case EP10:
	case EP11:
	case EP12:
	case EP13:
	case EP14:
	case EP15:
		break;
	}
}

void ProcessOutToken(void) {
	switch (USB_USTAT&0x78) {	// extract the EP bits
	case EP0:
		BD0O.bytecount = MAX_PACKET_SIZE;
		BD0O.status = 0x88;
		BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
		BD0I.status = 0xC8;		// send packet as DATA1, set UOWN bit
		break;
	case EP1:
		PORTB = BD1O.address[0];
		BD1O.bytecount = MAX_PACKET_SIZE;
		BD1O.status = 0x80;		BD0I.bytecount = 0x00;		// set EP0 IN byte count to 0
		BD0I.status = 0x80;	
		break;
	case EP2:
	case EP3:
	case EP4:
	case EP5:
	case EP6:
	case EP7:
	case EP8:
	case EP9:
	case EP10:
	case EP11:
	case EP12:
	case EP13:
	case EP14:
	case EP15:
		break;
	}
}

void SendDescriptorPacket(void) {
	unsigned char n;

	if (USB_bytes_left<MAX_PACKET_SIZE) {
		USB_dev_req = NO_REQUEST;	// sending a short packet, so clear device request
		USB_packet_length = USB_bytes_left;
		USB_bytes_left = 0x00;
	} else {
		USB_packet_length = MAX_PACKET_SIZE;
		USB_bytes_left -= MAX_PACKET_SIZE;
	}
	for (n = 0; n < USB_packet_length; n++) {
		BD0I.address[n] = *USB_desc_ptr++;
	}
	BD0I.bytecount = USB_packet_length;
	// toggle the DATA01 bit, clear the PIDs bits, and set the UOWN and DTS bits
	BD0I.status = ((BD0I.status ^ 0x40) & 0x40) | 0x88;
}

void main(void) {
	PORTA = 0x00;
	ADCON1 = 0x0F;		// set up PORTA to be digital I/Os
	TRISA = 0x00;			// set up all PORTA pins to be digital outputs

	InitUSB();			// initialize the USB registers and serial interface engine
	while (USB_USWSTAT != CONFIG_STATE) {		// while the peripheral is not configured...
		ServiceUSB();						// ...service USB requests
	}

	while (1) {
		ServiceUSB();
	}
}
