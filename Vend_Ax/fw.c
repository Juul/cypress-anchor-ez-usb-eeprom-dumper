//-----------------------------------------------------------------------------
//	File:		fw.c
//	Contents:	Firmware frameworks task dispatcher and device request parser
//				source.
//
//	Copyright (c) 2002 Cypress Semiconductor, Inc. All rights reserved
//
// $Archive: /USB/ez811/firmware/host/fw.c $
// $Date: 1/23/02 11:25a $
// $Revision: 4 $
//-----------------------------------------------------------------------------
#include "ezusb.h"
#include "ezregs.h"

//-----------------------------------------------------------------------------
// Random Macros
//-----------------------------------------------------------------------------
#define	min(a,b) (((a)<(b))?(a):(b))
#define	max(a,b) (((a)>(b))?(a):(b))

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------
volatile BOOL	GotSUD;
BOOL		Rwuen;
BOOL		Selfpwr;
volatile BOOL	Sleep;						// Sleep mode enable flag

WORD	pDeviceDscr;	// Pointer to Device Descriptor; Descriptors may be moved
WORD	pConfigDscr;	
WORD	pStringDscr;	

//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------
void SetupCommand(void);
void TD_Init(void);
void TD_Poll(void);
BOOL TD_Suspend(void);
BOOL TD_Resume(void);

BOOL DR_GetDescriptor(void);
BOOL DR_SetConfiguration(void);
BOOL DR_GetConfiguration(void);
BOOL DR_SetInterface(void);
BOOL DR_GetInterface(void);
BOOL DR_GetStatus(void);
BOOL DR_ClearFeature(void);
BOOL DR_SetFeature(void);
BOOL DR_VendorCmnd(void);

//-----------------------------------------------------------------------------
// Code
//-----------------------------------------------------------------------------

// Task dispatcher
void main(void)
{
	DWORD	i;
	WORD	offset;
	DWORD	DevDescrLen;
	DWORD	j=0;
	WORD	IntDescrAddr;
	WORD	ExtDescrAddr;

	// Initialize Global States
	Sleep = FALSE;					// Disable sleep mode
	Rwuen = FALSE;					// Disable remote wakeup
	Selfpwr = FALSE;				// Disable self powered
	GotSUD = FALSE;					// Clear "Got setup data" flag

	// Initialize user device
	TD_Init();

	// The following section of code is used to relocate the descriptor table. 
	// Since the SUDPTRH and SUDPTRL are assigned the address of the descriptor 
	// table, the descriptor table must be located in on-part memory.
	// The 4K demo tools locate all code sections in external memory.
	// The descriptor table is relocated by the frameworks ONLY if it is found 
	// to be located in external memory.
	pDeviceDscr = (WORD)&DeviceDscr;
	pConfigDscr = (WORD)&ConfigDscr;
	pStringDscr = (WORD)&StringDscr;
	if ((WORD)&DeviceDscr & 0xe000)
	{
		IntDescrAddr = INTERNAL_DSCR_ADDR;
		ExtDescrAddr = (WORD)&DeviceDscr;
		DevDescrLen = (WORD)&UserDscr - (WORD)&DeviceDscr + 2;
		for (i = 0; i < DevDescrLen; i++)
			*((BYTE xdata *)IntDescrAddr+i) = 0xCD;
		for (i = 0; i < DevDescrLen; i++)
			*((BYTE xdata *)IntDescrAddr+i) = *((BYTE xdata *)ExtDescrAddr+i);
		pDeviceDscr = IntDescrAddr;
		offset = (WORD)&DeviceDscr - INTERNAL_DSCR_ADDR;
		pConfigDscr -= offset;
		pStringDscr -= offset;
	}

	EZUSB_IRQ_ENABLE();				// Enable USB interrupt (INT2)
	EZUSB_ENABLE_RSMIRQ();				// Wake-up interrupt

	// The 8051 is responsible for all USB events, even those that have happened
	// before this point.  We cannot ignore pending USB interrupts.
	// The chip will come out of reset with the flags all cleared.
	//	USBIRQ = 0xff;				// Clear any pending USB interrupt requests
	PORTCCFG |= 0xc0;				// Turn on r/w lines for external memory 

	USBBAV = USBBAV | 1 & ~bmBREAK;	// Disable breakpoints and autovectoring
	USBIEN |= bmSUDAV | bmSUTOK | bmSUSP | bmURES;	// Enable selected interrupts
	EA = 1;						// Enable 8051 interrupts

   #ifndef NO_RENUM
   // Note: at full speed, high speed hosts may take 5 sec to detect device
   EZUSB_Discon(TRUE); // Renumerate
   #endif


	CKCON = (CKCON&(~bmSTRETCH)) | FW_STRETCH_VALUE; // Set stretch to 0 (after renumeration)

	// Task Dispatcher
	while(TRUE)					// Main Loop
	{
		if(GotSUD)				// Wait for SUDAV
		{
			SetupCommand();	 		// Implement setup command
  			GotSUD = FALSE;		   	// Clear SUDAV flag
		}

		// Poll User Device
		// NOTE: Idle mode stops the processor clock.  There are only two
		// ways out of idle mode, the WAKEUP pin, and detection of the USB
		// resume state on the USB bus.  The timers will stop and the
		// processor will not wake up on any other interrupts.
		if (Sleep)
		    {
    		if(TD_Suspend())
    		    { 
    		    Sleep = FALSE;	   		// Clear the "go to sleep" flag.  Do it here to prevent any race condition between wakeup and the next sleep.
    		    do
    		        {
       			    EZUSB_Susp();			// Place processor in idle mode.
    		        }
                while(!Rwuen && EZUSB_EXTWAKEUP());
                // Must continue to go back into suspend if the host has disabled remote wakeup
                // *and* the wakeup was caused by the external wakeup pin.
                
    			// 8051 activity will resume here due to USB bus or Wakeup# pin activity.
    			EZUSB_Resume();	// If source is the Wakeup# pin, signal the host to Resume.		
    			TD_Resume();
    		    }   
		    }
		TD_Poll();
	}
}

// Device request parser
void SetupCommand(void)
{
	void	*dscr_ptr;
	DWORD	i;

	switch(SETUPDAT[1])
	{
		case SC_GET_DESCRIPTOR:						// *** Get Descriptor
			if(DR_GetDescriptor())
				switch(SETUPDAT[3])			
				{
					case GD_DEVICE:				// Device
						SUDPTRH = MSB(pDeviceDscr);
						SUDPTRL = LSB(pDeviceDscr);
						break;
					case GD_CONFIGURATION:			// Configuration
						if(dscr_ptr = (void *)EZUSB_GetConfigDscr(SETUPDAT[2]))
						{
							SUDPTRH = MSB(dscr_ptr);
							SUDPTRL = LSB(dscr_ptr);
						}
						else
							EZUSB_STALL_EP0(); 	// Stall End Point 0
						break;
					case GD_STRING:				// String
						if(dscr_ptr = (void *)EZUSB_GetStringDscr(SETUPDAT[2]))
						{
							// Workaround for rev D errata number 8
							// If you're certain that you will never run on rev D,
							// you can just do this:
							// SUDPTRH = MSB(dscr_ptr);
							// SUDPTRL = LSB(dscr_ptr);
							STRINGDSCR *sdp;
							BYTE len;

							sdp = dscr_ptr;

							len = sdp->length;
							if (len > SETUPDAT[6]) 
								len = SETUPDAT[6]; //limit to the requested length
							
							while (len)
							{
								for(i=0; i<min(len,64); i++)
									*(IN0BUF+i) = *((BYTE xdata *)sdp+i);

								//set length and arm Endpoint
								EZUSB_SET_EP_BYTES(IN0BUF_ID,min(len,64));	
								len -= min(len,64);
                        (BYTE *)sdp += 64;

								// Wait for it to go out (Rev C and above)
								while(EP0CS & 0x04)
									;
							}

							// Arm a 0 length packet just in case.  There was some reflector traffic about
							// Apple hosts asking for too much data.  This will keep them happy and will
							// not hurt valid hosts because the next SETUP will clear this.
							EZUSB_SET_EP_BYTES(IN0BUF_ID,0);	
							// Clear the HS-nak bit
							EP0CS = bmHS;
						}
						else 
							EZUSB_STALL_EP0();	// Stall End Point 0
						break;
					default:				// Invalid request
						EZUSB_STALL_EP0();		// Stall End Point 0
				}
			break;
		case SC_GET_INTERFACE:						// *** Get Interface
			DR_GetInterface();
			break;
		case SC_SET_INTERFACE:						// *** Set Interface
			DR_SetInterface();
			break;
		case SC_SET_CONFIGURATION:					// *** Set Configuration
			DR_SetConfiguration();
			break;
		case SC_GET_CONFIGURATION:					// *** Get Configuration
			DR_GetConfiguration();
			break;
		case SC_GET_STATUS:						// *** Get Status
			if(DR_GetStatus())
				switch(SETUPDAT[0])
				{
					case GS_DEVICE:				// Device
						IN0BUF[0] = ((BYTE)Rwuen << 1) | (BYTE)Selfpwr;
						IN0BUF[1] = 0;
						EZUSB_SET_EP_BYTES(IN0BUF_ID,2);
						break;
					case GS_INTERFACE:			// Interface
						IN0BUF[0] = 0;
						IN0BUF[1] = 0;
						EZUSB_SET_EP_BYTES(IN0BUF_ID,2);
						break;
					case GS_ENDPOINT:			// End Point
						IN0BUF[0] = EPIO[EPID(SETUPDAT[4])].cntrl & bmEPSTALL;
						IN0BUF[1] = 0;
						EZUSB_SET_EP_BYTES(IN0BUF_ID,2);
						break;
					default:				// Invalid Command
						EZUSB_STALL_EP0();		// Stall End Point 0
				}
			break;
		case SC_CLEAR_FEATURE:						// *** Clear Feature
			if(DR_ClearFeature())
				switch(SETUPDAT[0])
				{
					case FT_DEVICE:				// Device
						if(SETUPDAT[2] == 1)
							Rwuen = FALSE; 		// Disable Remote Wakeup
						else
							EZUSB_STALL_EP0();	// Stall End Point 0
						break;
					case FT_ENDPOINT:			// End Point
						if(SETUPDAT[2] == 0)
                  {
							EZUSB_UNSTALL_EP( EPID(SETUPDAT[4]) );
                     EZUSB_RESET_DATA_TOGGLE( SETUPDAT[4] );
                  }
						else
							EZUSB_STALL_EP0();	// Stall End Point 0
						break;
				}
			break;
		case SC_SET_FEATURE:						// *** Set Feature
			if(DR_SetFeature())
				switch(SETUPDAT[0])
				{
					case FT_DEVICE:				// Device
						if(SETUPDAT[2] == 1)
							Rwuen = TRUE;		// Enable Remote Wakeup
						else
							EZUSB_STALL_EP0();	// Stall End Point 0
						break;
					case FT_ENDPOINT:			// End Point
						if(SETUPDAT[2] == 0)
							EZUSB_STALL_EP( EPID(SETUPDAT[4]) );
						else
							EZUSB_STALL_EP0();	 // Stall End Point 0
						break;
				}
			break;
		default:							// *** Invalid Command
			if(DR_VendorCmnd())
				EZUSB_STALL_EP0();				// Stall End Point 0
	}

	// Acknowledge handshake phase of device request
	// Required for rev C does not effect rev B
	EP0CS |= bmBIT1;
}

// Wake-up interrupt handler
void resume_isr(void) interrupt WKUP_VECT
{
	EZUSB_CLEAR_RSMIRQ();
}
