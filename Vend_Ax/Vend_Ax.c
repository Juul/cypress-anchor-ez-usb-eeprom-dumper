#pragma NOIV					// Do not generate interrupt vectors
//-----------------------------------------------------------------------------
//	File:		periph.c
//	Contents:	Hooks required to implement USB peripheral function.
//
//	Copyright (c) 1997 AnchorChips, Inc. All rights reserved
//-----------------------------------------------------------------------------
#include "ezusb.h"
#include "ezregs.h"

extern BOOL	GotSUD;			// Received setup data flag
extern BOOL	Sleep;
extern BOOL	Rwuen;
extern BOOL	Selfpwr;

BYTE	Configuration;		// Current configuration
BYTE	AlternateSetting;	// Alternate settings

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
#define	VR_UPLOAD		0xc0
#define VR_DOWNLOAD		0x40

#define VR_ANCHOR_DLD   0xa0 // handled by core
#define VR_EEPROM		0xa2 // loads (uploads) EEPROM
#define	VR_RAM			0xa3 // loads (uploads) external ram
#define VR_SETI2CADDR	0xa4
#define VR_GETI2C_TYPE  0xa5 // 8 or 16 byte address
#define VR_GET_CHIP_REV 0xa6 // Rev A, B = 0, Rev C = 2 // NOTE: New TNG Rev
#define VR_TEST_MEM     0xa7 // runs mem test and returns result
#define VR_RENUM	    0xa8 // renum
#define VR_DB_FX	    0xa9 // Force use of double byte address EEPROM (for FX)

#define SERIAL_ADDR		0x50
#define EP0BUFF_SIZE	0x40

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------
BYTE			DB_Addr;					//TPM Dual Byte Address stat
BYTE			I2C_Addr;					//TPM I2C address

BYTE	chip_select_bits = 0;

//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------
void EEPROMWrite(WORD addr, BYTE length, BYTE xdata *buf); //TPM EEPROM Write
//void EEPROMRead(WORD addr, BYTE length, BYTE xdata *buf);  //TPM EEPROM Read
void EEPROMRead(WORD addr, BYTE length, BYTE xdata *buf);

//-----------------------------------------------------------------------------
// Task Dispatcher hooks
//	The following hooks are called by the task dispatcher.
//-----------------------------------------------------------------------------

void TD_Init(void) 				// Called once at startup
{
	Rwuen = TRUE;				// Enable remote-wakeup
        EZUSB_InitI2C();			// Initialize I2C Bus
}

void TD_Poll(void) 				// Called repeatedly while the device is idle
{
}

BOOL TD_Suspend(void) 			// Called before the device goes into suspend mode
{
	return(TRUE);
}

BOOL TD_Resume(void) 			// Called after the device resumes
{
	return(TRUE);
}

//-----------------------------------------------------------------------------
// Device Request hooks
//	The following hooks are called by the end point 0 device request parser.
//-----------------------------------------------------------------------------

BOOL DR_GetDescriptor(void)
{
	return(TRUE);
}

BOOL DR_SetConfiguration(void)	// Called when a Set Configuration command is received
{
	Configuration = SETUPDAT[2];
	return(TRUE);				// Handled by user code
}

BOOL DR_GetConfiguration(void)	// Called when a Get Configuration command is received
{
	IN0BUF[0] = Configuration;
	EZUSB_SET_EP_BYTES(IN0BUF_ID,1);
	return(TRUE);				// Handled by user code
}

BOOL DR_SetInterface(void) 		// Called when a Set Interface command is received
{
	AlternateSetting = SETUPDAT[2];
	return(TRUE);				// Handled by user code
}

BOOL DR_GetInterface(void) 		// Called when a Set Interface command is received
{
	IN0BUF[0] = AlternateSetting;
	EZUSB_SET_EP_BYTES(IN0BUF_ID,1);
	return(TRUE);				// Handled by user code
}

BOOL DR_GetStatus(void)
{
	return(TRUE);
}

BOOL DR_ClearFeature(void)
{
	return(TRUE);
}

BOOL DR_SetFeature(void)
{
	return(TRUE);
}

BOOL DR_VendorCmnd(void)
{
//	return(TRUE);
//	EZUSB_Discon(TRUE);		// renumerate 

	WORD		addr, len, bc;
	WORD		ChipRev;
	WORD i;
	BYTE foo1 = 32;
	BYTE foo2 = 0;

	// Determine I2C boot eeprom device address; addr = 0x0 for 8 bit addr eeproms (24LC00)
	I2C_Addr = SERIAL_ADDR | ((I2CS & 0x10) >> 4); // addr=0x01 for 16 bit addr eeprom (LC65)
	// Indicate if it is a dual byte address part
	DB_Addr = (BOOL)(I2C_Addr & 0x01); //TPM: ID1 is 16 bit addr bit - set by rocker sw or jumper

	switch(SETUPDAT[1])
	{ //TPM handle new commands
		case VR_GETI2C_TYPE:
					*IN0BUF = DB_Addr;
					IN0BC = (BYTE)1; // Arm endpoint with # bytes to transfer
					EP0CS |= bmBIT1; // Acknowledge handshake phase of device request
			break;
		case VR_GET_CHIP_REV:
					ChipRev = GET_CHIP_REV();
					*IN0BUF = ChipRev;
					IN0BC = (BYTE)1; // Arm endpoint with # bytes to transferv
					EP0CS |= bmBIT1; // Acknowledge handshake phase of device request
			break;
		case VR_TEST_MEM:
					*IN0BUF = 0x0F; // Fail
					IN0BC = (BYTE)1; // Arm endpoint with # bytes to transfer
					EP0CS |= bmBIT1; // Acknowledge handshake phase of device request
			break;
		case VR_SETI2CADDR:
			I2C_Addr = SETUPDAT[2];
			break;
		case VR_RENUM:
					*IN0BUF = 7;
					IN0BC = (BYTE)1; // Arm endpoint with # bytes to transfer
					EP0CS |= bmBIT1; // Acknowledge handshake phase of device request
					EZUSB_Delay(1000);
					EZUSB_Discon(TRUE);		// renumerate until setup received
			break;
			
		case VR_DB_FX:
			DB_Addr = 0x01;		//TPM: need to assert double byte for FX board
			I2C_Addr |= 0x01;	//TPM: need to assert double byte for FX board
		case VR_RAM:
		case VR_EEPROM:
			addr = SETUPDAT[2];		// Get address and length
			addr |= SETUPDAT[3] << 8;
			len = SETUPDAT[6];
			len |= SETUPDAT[7] << 8;
//			len = 0xFF; // this doesn't work

			// Is this an upload command ?
			if(SETUPDAT[0] == VR_UPLOAD)
			{

//					len = 512;
					while(len)					// Move requested data through EP0IN 
					{							// one packet at a time.
	
						if(len < EP0BUFF_SIZE)
							bc = len;
						else
							bc = EP0BUFF_SIZE;
	
						// Is this a RAM upload ?
						if(SETUPDAT[1] == VR_RAM)
						{
							for(i=0; i<bc; i++)
								*(IN0BUF+i) = *((BYTE xdata *)addr+i);
						}
						else
						{
	//						for(i=0; i<bc; i++)
	//							*(IN0BUF+i) = 0xcc;
							EEPROMRead(addr,(WORD)bc,(WORD)IN0BUF);

						}
	
						IN0BC = (BYTE)bc; // Arm endpoint
	
						addr += bc;
						if(bc > len) {
							len = 0;
						} else {
							len -= bc;
						}
						
	
	                                        // NOTE: New TNG Rev
						//ChipRev = GET_CHIP_REV();
						//if(ChipRev <= EZUSB_CHIPREV_B)
						//	while(EP0CS & 0x02);
						//else // Rev C and above
						while(EP0CS & 0x04);
					}
					EZUSB_Delay(100);


			}
			// Is this a download command ?
			else if(SETUPDAT[0] == VR_DOWNLOAD)
			{
				while(len)					// Move new data through EP0OUT 
				{							// one packet at a time.
					// Arm endpoint - do it here to clear (after sud avail)
					OUT0BC = 0;  // Clear bytecount to allow new data in; also stops NAKing

                                        // NOTE: New TNG Rev
					//ChipRev = GET_CHIP_REV();
					//if(ChipRev <= EZUSB_CHIPREV_B)
					//	while(OUT0CS & 0x02);
					//else  // Rev C and above
						while(EP0CS & 0x08);

					bc = OUT0BC; // Get the new bytecount

					// Is this a RAM download ?
					if(SETUPDAT[1] == VR_RAM)
					{
						for(i=0; i<bc; i++)
							 *((BYTE xdata *)addr+i) = *(OUT0BUF+i);
					}
					else
						EEPROMWrite(addr,bc,(WORD)OUT0BUF);

					addr += bc;
					len -= bc;
				}
			}
			break;
	}
	return(FALSE); // no error; command handled OK
}

//-----------------------------------------------------------------------------
// USB Interrupt Handlers
//	The following functions are called by the USB interrupt jump table.
//-----------------------------------------------------------------------------

// Setup Data Available Interrupt Handler
void ISR_Sudav(void) interrupt 0
{
	GotSUD = TRUE;				// Set flag
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUDAV;			// Clear SUDAV IRQ
}

// Setup Token Interrupt Handler
void ISR_Sutok(void) interrupt 0
{
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUTOK;			// Clear SUTOK IRQ
}

void ISR_Sof(void) interrupt 0
{
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSOF;				// Clear SOF IRQ
}

void ISR_Ures(void) interrupt 0
{
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmURES;			// Clear URES IRQ
}

void ISR_IBN(void) interrupt 0
{
   // ISR for the IN Bulk NAK (IBN) interrupt.
}

void ISR_Susp(void) interrupt 0
{
	Sleep = TRUE;
	EZUSB_IRQ_CLEAR();
	USBIRQ = bmSUSP;
}

void ISR_Ep0in(void) interrupt 0
{
}

void ISR_Ep0out(void) interrupt 0
{
}

void ISR_Ep1in(void) interrupt 0
{
}

void ISR_Ep1out(void) interrupt 0
{
}

void ISR_Ep2in(void) interrupt 0
{
}

void ISR_Ep2out(void) interrupt 0
{
}

void ISR_Ep3in(void) interrupt 0
{
}

void ISR_Ep3out(void) interrupt 0
{
}

void ISR_Ep4in(void) interrupt 0
{
}

void ISR_Ep4out(void) interrupt 0
{
}

void ISR_Ep5in(void) interrupt 0
{
}

void ISR_Ep5out(void) interrupt 0
{
}

void ISR_Ep6in(void) interrupt 0
{
}

void ISR_Ep6out(void) interrupt 0
{
}

void ISR_Ep7in(void) interrupt 0
{
}

void ISR_Ep7out(void) interrupt 0
{
}

void EEPROMWriteByte(WORD addr, BYTE value)
{
	BYTE		i = 0;
	BYTE xdata 	ee_str[3];

	if(DB_Addr)
		ee_str[i++] = MSB(addr);

	ee_str[i++] = LSB(addr);
	ee_str[i++] = value;

	EZUSB_WriteI2C(I2C_Addr, i, ee_str);
   EZUSB_WaitForEEPROMWrite(I2C_Addr);
}


void EEPROMWrite(WORD addr, BYTE length, BYTE xdata *buf)
{
	BYTE	i;
	for(i=0;i<length;++i)
		EEPROMWriteByte(addr++,buf[i]);
}

/*
void EEPROMRead(WORD addr, BYTE length, BYTE xdata *buf)
{
	BYTE		i = 0;
	BYTE		j = 0;
	BYTE xdata 	ee_str[2];

	if(DB_Addr)
		ee_str[i++] = MSB(addr);

	ee_str[i++] = LSB(addr);

	EZUSB_WriteI2C(I2C_Addr, i, ee_str);

//	for(j=0; j < length; j++)
//		*(buf+j) = 0xcd;

	EZUSB_ReadI2C(I2C_Addr, length, buf);
}
*/

void EEPROMRead(WORD addr, BYTE length, BYTE xdata *buf)
{
//	BYTE		i = 0;
	BYTE		j = 0;
	BYTE xdata 	ee_str[2];
//	BYTE		i2c_addr_write;
	BYTE		i2c_addr;
	// For two byte address EEPROMS (which the 24LC256 is) 
	// the address bits must be set to 1
    // See Page 74 of the Cypress AN2131 manual
	BYTE		chip_select = 1; 
	BOOL		ret;

	ee_str[0] = MSB(addr);
	ee_str[1] = LSB(addr);

	// All addresses begin with the four bits: 1010 (01010000 is 0x50)
	// The last three bits of the i2c_addr are the chip select
        // For large two-byte-address EEPROMs it must be 001, so the i2c address is 0x51
        // Note that the address provided is 7 bits that are bit-shifted one bit left
        // by the EZUSB library. The last bit (read or write) is provided by the library
	i2c_addr = 0x51;

	for(j=0; j < length; j++)
		*(buf+j) = 0;

	ret = EZUSB_WriteI2C(i2c_addr, 2, ee_str);
	if(ret == TRUE) {
//		*(buf+0) = 0xA;
	} else { 
//		*(buf+0) = 0xF;
	}
	EZUSB_Delay(100);



//	*(buf+0) = ee_str[0];
//	*(buf+1) = ee_str[1];

	ret = EZUSB_ReadI2C(i2c_addr, length, buf);

	if(ret == TRUE) {
//		*(buf+1) = 0xA;
	} else { 
//		*(buf+1) = 0xF;
	}

//	EZUSB_Delay(500);
	// each time this is called, go to the next chip_select address
//	chip_select_bits = (chip_select_bits + 1) % 8;
}



