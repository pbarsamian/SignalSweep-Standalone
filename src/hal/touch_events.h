// SPDX-License-Identifier: GPL-3.0-or-later
// touch_events.h — touch as a stream of press / move / release events (boards with swipe navigation: the C6).
// touch_get() in touch.h stays the plain "is a finger down, and where" call; both are implemented by the board.
// Coordinates are screen coordinates in the UI's orientation, same as touch_get().
#pragma once
#include <stdint.h>

enum TouchKind : uint8_t { TE_DOWN = 1, TE_MOVE = 2, TE_UP = 3 };

struct TouchEvent {
    uint8_t  kind;
    int16_t  x, y;      // for TE_UP: the last position seen
    uint32_t ms;
};

// Returns true and fills `e` when there is an event; call until it returns false. UI task only.
bool touch_poll(TouchEvent& e);
