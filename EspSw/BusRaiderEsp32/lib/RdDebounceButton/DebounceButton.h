// DebounceButton
// Rob Dobson 2018

// Callback on button press via a hardware pin

#pragma once

#include "Arduino.h"
#include <functional>

typedef std::function<void(int val)> DebounceButtonCallback;

class DebounceButton
{
private:
    // Button
    int _buttonPin;
    int _buttonActiveLevel;
    uint32_t _debounceLastMs;
    int _debounceVal;
    static const int PIN_DEBOUNCE_MS = 50;

    // Callback
    DebounceButtonCallback _callback;

public:
    DebounceButton()
    {
        _buttonPin = -1;
        _buttonActiveLevel = 0;
        _debounceLastMs = 0;
        _debounceVal = 0;
    }

    // Setup
    void setup(int pin, int activeLevel, DebounceButtonCallback cb);
    // Service - must be called frequently to check button state
    void service();
};
