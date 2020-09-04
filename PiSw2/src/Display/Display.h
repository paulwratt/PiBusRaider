// Bus Raider
// Rob Dobson 2019

#pragma once

#include "wgfxfont.h"
#include "stdint.h"
#include "DisplayBase.h"
#include "DisplayFX.h"

class Display : public DisplayBase
{
public:
    enum STATUS_FIELD_ELEMENTS
    {
        STATUS_FIELD_PI_VERSION,
        STATUS_FIELD_LINKS,
        STATUS_FIELD_ESP_VERSION,
        STATUS_FIELD_IP_ADDR,
        STATUS_FIELD_CUR_MACHINE,
        STATUS_FIELD_MACHINES,
        STATUS_FIELD_BUS_ACCESS,
        STATUS_FIELD_REFRESH_RATE,
        STATUS_FIELD_KEYBOARD,
        STATUS_FIELD_ASSERTS,
        STATUS_FIELD_FILE_STATUS,
        STATUS_FIELD_NUM_ELEMENTS
    };

    static const int STATUS_NORMAL = 0;
    static const int STATUS_FAIL = 1;
    static const int STATUS_HILITE = 2;

public:
    Display();
    ~Display();

    bool init();

    // Target
    void targetLayout(
                    int pixX, int pixY, 
                    int cellX, int cellY, 
                    int xScale, int yScale,
                    WgfxFont* pFont, 
                    int foreColour, int backColour) override;

    // Status
    void statusPut(int statusElement, int statusType, const char* pStr);

    // Window
    void windowForeground(int winIdx, DISPLAY_FX_COLOUR colour);
    void windowBackground(int winIdx, DISPLAY_FX_COLOUR colour);
    void windowWrite(int winIdx, int col, int row, const char* pStr);
    void windowWrite(int winIdx, int col, int row, int ch);
    void windowSetPixel(int winIdx, int x, int y, int value, DISPLAY_FX_COLOUR colour);

    // Console
    void consolePut(const char* pStr);
    void consolePut(int ch);
    void consolePut(const char* pBuffer, unsigned count);
    void consoleForeground(DISPLAY_FX_COLOUR colour);
    int consoleGetWidth();
 
    // Log debug
    void logDebug(const char* pSeverity, const char* pSource, const char* pMsg);

    // Target window
    void foreground(DISPLAY_FX_COLOUR colour) override;
    void background(DISPLAY_FX_COLOUR colour) override;
    void write(int col, int row, const char* pStr) override;
    void write(int col, int row, int ch) override;
    void setPixel(int x, int y, int value, DISPLAY_FX_COLOUR colour) override;

    // Implementations of CDevice base class
    int Write(const void *pBuffer, size_t nCount);

    // RAW access
    void getFrameBufferInfo(FrameBufferInfo& frameBufferInfo) override;

	/// \brief Displays rotating symbols in the upper right corner of the screen
	/// \param nIndex Index of the rotor to be displayed (0..3)
	/// \param nCount Phase (angle) of the current rotor symbol (0..3)
	void rotor(unsigned nIndex, unsigned nCount)
    {
    }

private:

    // DisplayFX
    DisplayFX _displayFX;

    // Layout
    static const int TARGET_WINDOW_BORDER_PIX = 5;

    // Flag
    bool _displayStarted;

    // Current status field values
    static const int MAX_STATUS_FIELD_STRLEN = 100;
    char _statusFieldStrings[STATUS_FIELD_NUM_ELEMENTS][MAX_STATUS_FIELD_STRLEN];
};