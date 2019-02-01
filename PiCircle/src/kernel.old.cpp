//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2016  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "kernel.h"
#include <circle/usb/usbkeyboard.h>
#include <circle/string.h>
#include <assert.h>
#include <string.h>

#ifdef NEWCODE
#include <circle/util.h>
#include "testTiming.h"
#include "Target/TargetScreen.h"
#include "Target/TargetFonts.h"
#include "Fonts/FontTRS80Level1.h"
#include "System/lowlev.h"

void DoChangeMachine(const char* mcName)
{

}
else
#include <circle/debug.h>
#include <circle/timer.h>

#endif

static const char FromKernel[] = "kernel";

CKernel *CKernel::s_pThis = 0;

#ifndef NEWCODE
int CKernel::_frameCount = 0;
#endif

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Serial (&m_Interrupt),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_DWHCI (&m_Interrupt, &m_Timer),
	m_ShutdownMode (ShutdownNone),
#ifdef NEWCODE
	_commandHandler(m_Serial, DoChangeMachine)
#else
	_miniHDLC(miniHDLCPutCh, miniHDLCFrameRx)
#endif
{
	s_pThis = this;

	// m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
	s_pThis = 0;
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}
	
	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}
	
	if (bOK)
	{
		CDevice *pTarget = 0;  m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Serial;
		}

		bOK = m_Logger.Initialize (pTarget);
	}
	
	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_DWHCI.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
#ifdef NEWCODE
	m_Screen.Write("\E[17;40r",8);

	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
#endif

	CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *) m_DeviceNameService.GetDevice ("ukbd1", FALSE);
	if (pKeyboard == 0)
	{
		m_Logger.Write (FromKernel, LogError, "Keyboard not found");
	}

#if 1	// set to 0 to test raw mode
	pKeyboard->RegisterShutdownHandler (ShutdownHandler);
	pKeyboard->RegisterKeyPressedHandler (KeyPressedHandler);
#else
	pKeyboard->RegisterKeyStatusHandlerRaw (KeyStatusHandlerRaw);
#endif

#ifdef NEWCODE
	m_Logger.Write (FromKernel, LogNotice, "Just type something!");

	TargetFonts targetFonts;
	targetFonts.addFont(FontTRS80Level1::getFont());
	TargetScreen targetScreen(m_Screen, targetFonts);
	targetScreen.setup(0,0,64,16,"TRS80Level1", HIGH_COLOR, BLACK_COLOR, 0, 0, 2, 2);

	u32 mic = micros();
	// int rxCount = 0;
	// int rxTotal = 0;
	while(1)
	{
		if (micros() > mic + 1000000)
		{
			// writeStr("{\"cmdName\":\"apiReq\",\"req\":\"queryESPHealth\"}");
			m_Logger.Write(FromKernel, LogNotice, "num %d tot %d frameData %d", _commandHandler.numRead, 
						_commandHandler.totalRead, _commandHandler.bytesRx);
			_commandHandler.numRead = 0;
			// m_Logger.Write (FromKernel, LogNotice, "rx %d total %d", rxCount, rxTotal);
			// rxCount = 0;
			mic = micros();
		}
		_commandHandler.service();
		// unsigned char buf[1000];
		// int numRead = m_Serial.Read(buf, sizeof buf);
		// rxCount += numRead;
		// rxTotal += numRead;
	}

	// uint32_t curMicros = micros();
	// while (1)
	// {

	// 	if (micros() > curMicros + 1000000)
	// 	{
	// 		uint8_t pData[1024];
	// 		_busRaider.blockRead(0, pData, 1024, true, false);
	// 		for (int i = 0; i < 16; i++)
	// 		{
	// 			for (int j = 0; j < 64; j++)
	// 			{
	// 				targetScreen.putChar(pData[i*64+j], j, i);
	// 			}
	// 		}
	// 		m_Logger.Write(FromKernel, LogNotice, "num %d tot %d", _commandHandler.numRead, _commandHandler.totalRead);
	// 		_commandHandler.numRead = 0;
	// 		curMicros = micros();
	// 	}
	// 	// CTimer::SimpleMsDelay(100);

	// 	_commandHandler.service();
	// }
		// uint32_t nowM = micros();
		// lowlevCycleDelay(1000000);
		// uint32_t diffM = micros() - nowM;
		// m_Logger.Write(FromKernel, LogNotice, "uS %d", diffM);

	// targetScreen.putChar('A',0,0);
	// targetScreen.putChar('A',40,0);

	// for (unsigned nCount = 0; m_ShutdownMode == ShutdownNone; nCount++)
	// {
	// 	// CUSBKeyboardDevice::UpdateLEDs() must not be called in interrupt context,
	// 	// that's why this must be done here. This does nothing in raw mode.
	// 	pKeyboard->UpdateLEDs ();

	// 	m_Screen.Rotor (0, nCount);
	// 	m_Timer.MsDelay (100);

	// 	m_Screen.Write("Hello\n", 6);
	// 	testTiming(1);

	// }
	return ShutdownReboot;
#else
	int rxTotal = 0;
	int rxLoop = 0;
	int maxLoops = 3;
	for (int i = 0; i < maxLoops; i++)
	{
		// m_Serial.Write("Listening\n", 10);
		// int curTicks = m_Timer.GetTicks();
		rxLoop = 0;
		// while (m_Timer.GetTicks() < curTicks + 5000)
		// {
		unsigned int curTicks = m_Timer.GetTicks();
		while(1)
		{
			unsigned char buf[1000];
			int numRead = m_Serial.Read(buf, sizeof buf);
			if (numRead > 0)
			{
				rxTotal += numRead;
				rxLoop += numRead;
				_miniHDLC.handleBuffer(buf, numRead);
			}
			if (m_Timer.GetTicks() > curTicks + 1000)
				break;
		// }
		}
			// const char* sss = "Compile time: " __DATE__ " " __TIME__ "\n";
			// m_Serial.Write(sss, strlen(sss));
			
			// Receive chars

		CString sss;
		m_Logger.Write(FromKernel, LogNotice, "Hello there");
		sss.Format("Loop %d (of %d) Received bytes %d (total %d) Time in ticks %d ~~~\n", i+1, maxLoops, rxLoop, rxTotal, m_Timer.GetTicks());
		// const char* sss = "Compile time: " __DATE__ " " __TIME__ "\n";
		m_Serial.Write((const char*)sss, sss.GetLength());
		// m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);
	}

	m_Serial.Write("Restarting\n", 11);
	m_Serial.Flush();

// 	// show the character set on screen
// 	for (char chChar = ' '; chChar <= '~'; chChar++)
// 	{
// 		if (chChar % 8 == 0)
// 		{
// 			m_Screen.Write ("\n", 1);
// 		}

// 		CString Message;
// 		Message.Format ("%02X: \'%c\' ", (unsigned) chChar, chChar);
		
// 		m_Screen.Write ((const char *) Message, Message.GetLength ());
// 	}
// 	m_Screen.Write ("\n", 1);

// #ifndef NDEBUG
// 	// some debugging features
// 	m_Logger.Write (FromKernel, LogDebug, "Dumping the start of the ATAGS");
// 	debug_hexdump ((void *) 0x100, 128, FromKernel);

// 	m_Logger.Write (FromKernel, LogNotice, "The following assertion will fail");
// 	// assert (1 == 2);
// #endif
	// CTimer::SimpleMsDelay(1000);
	// }
	return ShutdownReboot;
#endif
}

#ifdef NEWCODE
int curPos = 0;
#else
void CKernel::miniHDLCPutCh(uint8_t ch)
{

}

void CKernel::miniHDLCFrameRx(const uint8_t *framebuffer, int framelength)
{
	assert (s_pThis != 0);
	_frameCount++;
	s_pThis->m_Logger.Write(FromKernel, LogNotice, "Got frame %d", _frameCount);
}
#endif

void CKernel::KeyPressedHandler (const char *pString)
{
	assert (s_pThis != 0);
#ifdef NEWCODE
	// s_pThis->m_Screen.Write (pString, strlen (pString));
	uint8_t pD[] = "A";
	pD[0] = pString[0];
	s_pThis->_busRaider.blockWrite(curPos++, pD, 1, true, false);
#else
	s_pThis->m_Serial.Write (pString, strlen (pString));
#endif

}

void CKernel::ShutdownHandler (void)
{
	assert (s_pThis != 0);
	s_pThis->m_ShutdownMode = ShutdownReboot;
}

void CKernel::KeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	assert (s_pThis != 0);

	CString Message;
	Message.Format ("Key status (modifiers %02X)", (unsigned) ucModifiers);

	for (unsigned i = 0; i < 6; i++)
	{
		if (RawKeys[i] != 0)
		{
			CString KeyCode;
			KeyCode.Format (" %02X", (unsigned) RawKeys[i]);

			Message.Append (KeyCode);
		}
	}

	s_pThis->m_Logger.Write (FromKernel, LogNotice, Message);
}


#ifdef OLD_BOLD

#include "kernel.h"
#include <circle/gpiopin.h>
#include <circle/timer.h>
#include <circle/machineinfo.h>
#include <string.h>

#include "TargetBus/busraider.h"

static const char FromKernel[] = "kernel";

void DoChangeMachine(const char* mcName)
{

}

CKernel::CKernel (void)
:	m_Memory (TRUE),
	m_Screen (1366,768),
	m_Logger (4)
// :
// 	m_commandHandler(DoChangeMachine)
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}
	
	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}
	
	m_Screen.Write("Here\n",5);

	CMachineInfo* pMCInfo = CMachineInfo::Get();
	if (pMCInfo)
	{
		// const char* pMC = pMCInfo->GetMachineName();
		m_Screen.Write("Here2\n",5);
		// m_Screen.Write(pMC, strlen(pMC));
		int model = pMCInfo->GetMachineModel();
		char pb[] = "A\n";
		pb[0] = 'A' + model;
		m_Screen.Write(pb,3);
	}

	//m_Logger.Initialize (&m_Screen);

	// if (bOK)
	// {
	// 	CDevice *pTarget = &m_Serial;
	// 	bool bbb = m_Logger.Initialize (pTarget);
	// 	if (!bbb)
	// 		m_Screen.Write("Fail\n",5);
	// }
	
	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Screen.Write ("hello\n", 6);

	// m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

    // Bus raider setup
    // _busRaider.init();

	// static const uint8_t program[] = { 0x21, 0x00, 0x3c, 0x11, 0x01, 0x3C, 0x36, 0x22, 0x01, 0x00, 0x44, 0xED, 0xB0, 0xC3, 0x0D, 0x00 };

	// pinRawMode(8, false, 0);
	// for (int i = 0; i < 10; i++)
	// {
	// 	pinRawWrite(8, 1);
	// 	CTimer::SimpleusDelay(1);
	// 	pinRawWrite(8, 0);
	// 	CTimer::SimpleusDelay(1);
	// }

	// CGPIOPin AudioLeft (GPIOPinAudioLeft, GPIOModeOutput);
	// CGPIOPin AudioRight (GPIOPinAudioRight, GPIOModeOutput);

	// // _busRaider.blockWrite(0, program, sizeof(program), true, false);
	// // _busRaider.hostReset();

	// // flash the Act LED 10 times and click on audio (3.5mm headphone jack)
	// for (unsigned i = 1; i <= 10; i++)
	// {
	// 	m_ActLED.On ();
	// 	AudioLeft.Invert ();
	// 	AudioRight.Invert ();
	// 	CTimer::SimpleMsDelay (200);

	// 	m_ActLED.Off ();
	// 	CTimer::SimpleMsDelay (500);

	// 	m_Screen.Write ("hello\n", 6);

	// // 	_busRaider.controlReqAndTake();
	// // 	_busRaider.controlRelease(0);

	// }

	CTimer::SimpleMsDelay(5000);

	return ShutdownReboot;
}

#endif