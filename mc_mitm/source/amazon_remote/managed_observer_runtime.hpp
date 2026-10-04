// SPDX-License-Identifier: GPL-2.0-only
// Passive diagnostics only: no driver scan, GATT, bond, or HID-output ownership.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ams::mc::amazon_observer {
// Startup/Main thread only, before the Bluetooth producer starts.
void Initialize();
// Main thread only. Explicit file opt-in, finite boot session, no automatic rearm.
void Start();
void Poll();
void Suspend();
// Existing BLE event thread only. Bounded copy; no filesystem/IPC/heap/wait.
void Observe(std::uint32_t type, const void *event, std::size_t size);
// Read-only exact-target diagnostic snapshot for the foreground link test.
// No scan, filter, connection, pairing or output command is issued here.
bool ReadTargetSighting(void *out, std::size_t size);
// MC IPC thread: one-shot passive lease only. No radio or filesystem calls.
bool ArmTargetCapture();
void EndTargetCapture();
bool ReadCaptureStatus(void *out, std::size_t size);
}
