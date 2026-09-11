///////////////////////////////////////////////////////////////////////////////////////////////////
// rfid_switch_core2_led.h
//
// Lightweight M5Stack Core2 power LED control.
//
// This API controls the Core2 power-management LED directly over I2C without
// including M5Unified. It detects the installed AXP192 or AXP2101 PMIC and
// uses the corresponding LED control register.
//
// https://github.com/matthias-bs/esp32-rfid-switch
//
//
// created: 09/2026
//
//
// MIT License
//
// Copyright (c) 2026 Matthias Prinke
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// History:
//
// 20260911 Initial
//
// ToDo:
// -
//
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Arduino.h>

namespace rfid_switch_core2_led
{
#if defined(ARDUINO_M5STACK_CORE2)
bool begin();
void set(bool enabled);
void blinkFailure();
void startConfigurationBlink();
void blinkConfiguration();
#else
inline bool begin()
{
    return false;
}

inline void set(bool)
{
}

inline void blinkFailure()
{
}

inline void startConfigurationBlink()
{
}

inline void blinkConfiguration()
{
}
#endif
}
