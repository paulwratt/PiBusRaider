// Bus Raider
// Rob Dobson 2018

#include "globaldefs.h"
#include "utils.h"
#include "uart.h"
#include "timer.h"
#include "wgfx.h"
#include "ee_printf.h"
#include "piwiring.h"
#include "busraider.h"
#include "cmd_handler.h"
#include "target_memory_map.h"
#include "rdutils.h"
#include "../uspi\include\uspi\types.h"
#include "../uspi/include/uspi.h"
#include "McManager.h"
#include "McTRS80.h"
#include "McRobsZ80.h"
#include "McZXSpectrum.h"
#include "McTerminal.h"

// Baud rate
#define MAIN_UART_BAUD_RATE 115200

static void _keypress_raw_handler(unsigned char ucModifiers, const unsigned char rawKeys[6])
{
    // ee_printf("KEY mod %02x raw %02x %02x %02x\n", ucModifiers, rawKeys[0], rawKeys[1], rawKeys[2]);
    McBase* pMc = McManager::getMachine();
    if (pMc)
        pMc->keyHandler(ucModifiers, rawKeys);
}

void layout_display()
{
    // Clear display
    wgfx_clear();
    
    // Layout display
    McDescriptorTable* pMcDescr = McManager::getDescriptorTable(0);
    int windowBorderWidth = 5;
    wgfx_set_window(0, -1, 0, 
        pMcDescr->displayPixelsX, pMcDescr->displayPixelsY,
        pMcDescr->displayCellX, pMcDescr->displayCellY,
        pMcDescr->pixelScaleX, pMcDescr->pixelScaleY,
        pMcDescr->pFont, pMcDescr->displayForeground, pMcDescr->displayBackground,
        windowBorderWidth, 8);
    wgfx_set_window(1, 0, (pMcDescr->displayPixelsY*pMcDescr->pixelScaleY) + windowBorderWidth*2 + 10, 
        -1, -1, -1, -1, 1, 1, 
        NULL, -1, -1,
        0, 8);
    wgfx_set_console_window(1);
}

extern "C" void set_machine_by_name(const char* mcName)
{
    // Set the machine
    if (McManager::setMachineByName(mcName))
    {
        // Set the display
        layout_display();
    }

}

extern "C" void entry_point()
{

    // Logging
    LogSetLevel(LOG_DEBUG);
    
    // System init
    system_init();

    // Init timers
    timers_init();

    // UART
    uart_init(MAIN_UART_BAUD_RATE, 1);

    // Target machine memory and command handler
    targetClear();
    cmdHandler_init(set_machine_by_name);

    // Init machine manager
    McManager::init();

    // Add machines
    new McTRS80();
    new McRobsZ80();
    new McZXSpectrum();
    new McTerminal();

    // Initialise graphics system
    wgfx_init(1366, 768);

    // Layout display for the selected machine
    layout_display();

    // Initial message
    wgfx_set_fg(11);
    ee_printf("RC2014 Bus Raider V1.6.001\n");
    wgfx_set_fg(15);
    ee_printf("Rob Dobson 2018 (inspired by PiGFX)\n\n");

    // Number of machines
    ee_printf("%d machines supported\n", McManager::getNumMachines());

    // Bus raider setup
    br_init();

    // Enable first machine
    McManager::setMachineIdx(0);

    // Get current machine to check things are working
    if (!McManager::getMachine())
    {
        ee_printf("Failed to construct default machine\n");
    }

    // USB
    if (USPiInitialize()) 
    {
        ee_printf("Checking for keyboards...");

        if (USPiKeyboardAvailable()) 
        {
            USPiKeyboardRegisterKeyStatusHandlerRaw(_keypress_raw_handler);
            ee_printf("keyboard found\n");
        } else 
        {
            wgfx_set_fg(9);
            ee_printf("keyboard not found\n");
            wgfx_set_fg(15);
        }
    } else 
    {
        ee_printf("USB initialization failed\n");
    }
    ee_printf("\n");

    // Debug show colour palette
    // for (int i = 0; i < 255; i++)
    // {
    //     wgfx_set_fg(i);
    //     ee_printf("%02d ", i);
    // }
    // wgfx_set_fg(15);

    // Waiting...
    ee_printf("Waiting for UART data (%d,8,N,1)\n", MAIN_UART_BAUD_RATE);

    // Refresh rate
    McDescriptorTable* pMcDescr = McManager::getDescriptorTable(0);
    const unsigned long reqUpdateUs = 1000000 / pMcDescr->displayRefreshRatePerSec;
    #define REFRESH_RATE_WINDOW_SIZE_MS 2000
    int refreshCount = 0;
    unsigned long curRateSampleWindowStart = micros();
    unsigned long lastDisplayUpdateUs = 0;
    unsigned long dispTime = 0;

    // Status update rate and last timer
    unsigned long lastStatusUpdateMs = 0;
    const unsigned long STATUS_UPDATE_RATE_MS = 3000;

    // Loop forever
    while (1) 
    {

        // Handle target machine display updates
        if (timer_isTimeout(micros(), lastDisplayUpdateUs, reqUpdateUs)) 
        {
            // Check valid
            dispTime = micros();
            lastDisplayUpdateUs = micros();
            McBase* pMc = McManager::getMachine();
            if (pMc)
                pMc->displayRefresh();
            dispTime = micros() - dispTime;
            refreshCount++;
        }

        // Handle refresh rate calculation
        if (timer_isTimeout(micros(), curRateSampleWindowStart, REFRESH_RATE_WINDOW_SIZE_MS * 1000)) 
        {
            int refreshRate = refreshCount * 1000 / REFRESH_RATE_WINDOW_SIZE_MS;
            const int MAX_REFRESH_STR_LEN = 20;
            char refreshStr[MAX_REFRESH_STR_LEN+1];
            rditoa(refreshRate, (uint8_t*)refreshStr, MAX_REFRESH_STR_LEN, 10);
            strncpy(refreshStr+strlen(refreshStr), "fps", MAX_REFRESH_STR_LEN);
            wgfx_puts(1, wgfx_get_term_width()-strlen(refreshStr)-1, 0, (uint8_t*)refreshStr);
            // // uart_printf("Rate %d per sec, requs %ld dispTime %ld\n", refreshCount / 2, reqUpdateUs, dispTime);
            // wgfx_putc(1, 150, 0, '0' + (refreshRate % 10));
            // wgfx_putc(1, 149, 0, '0' + ((refreshRate / 10) % 10));
            // if (refreshRate / 100 != 0)
            //     wgfx_putc(1, 148, 0, '0' + ((refreshRate / 100) % 10));
            refreshCount = 0;
            curRateSampleWindowStart = micros();
        }

        // Handle status update to ESP32
        if (timer_isTimeout(micros(), lastStatusUpdateMs, STATUS_UPDATE_RATE_MS * 1000)) 
        {
            // Send status update
            cmdHandler_sendStatusUpdate();
            lastStatusUpdateMs = micros();
        }

        // Service command handler
        cmdHandler_service();

        // Timer polling
        timer_poll();

        // Service bus raider
        br_service();
    }
}

// #endif