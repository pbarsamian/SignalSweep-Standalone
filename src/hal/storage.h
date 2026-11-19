// SPDX-License-Identifier: GPL-3.0-or-later
// storage.h — storage HAL (INTERFACE ONLY, not implemented or included yet). The standalone firmware has no SD
// code in v3.21; Phase 5 adds it, borrowed from the post-3.21 SignalSweep-CYD work (docs/port/SD_BORROW.md).
// lock()/unlock() exist for the C6, where the LCD and the TF slot share SCK/MOSI. They are no-ops on the CYD.
#pragma once
bool storage_mount();
void storage_lock();
void storage_unlock();
