
#ifndef _kernel_h
#define _kernel_h

#include <circle/memory.h>
#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include "system/FastScreen.h"
#include <circle/serial.h>
#include "system/UartMaxiSerialDevice.h"
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <circle/types.h>
#include "comms/CommsManager.h"
#include "comms/CommandHandler.h"
#include "Hardware/HwManager.h"
#include "TargetBus/BusAccess.h"
#include "BusControlAPI/BusControlAPI.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	// do not change this order
	CMemorySystem		m_Memory;
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	FastScreen		m_Screen;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CUartMaxiSerialDevice		m_Serial;
	CTimer			m_Timer;
	CLogger			m_Logger;
	CUSBHCIDevice		m_USBHCI;
	CScheduler		m_Scheduler;
	CNetSubSystem		m_Net;

	// Pointer to singleton kernel
	static CKernel* m_pKernel;

	// Comms Manager
	CommsManager m_CommsManager;

	// BusAccess
	BusAccess m_BusAccess;

	// Hardware manager
	HwManager m_HwManager;

	// BusControlAPI
	BusControlAPI m_BusControlAPI;
};

#endif