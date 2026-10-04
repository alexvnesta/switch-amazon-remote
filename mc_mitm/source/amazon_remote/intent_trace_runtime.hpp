// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intent_trace.hpp"
#include "ble_header_trace.hpp"
namespace ams::mc::ble_intent_trace {
// Optional future integration: Main/Startup only, before MITM thread starts.
// Default disabled. Reads explicit opt-in; immutable 120-second absolute boot
// deadline applies in BOTH producers, even if LaunchModules blocks.
// No global Bluetooth settings changed. Disable is also safe from producers.
void Initialize();
// No automatic rearming policy is supplied here.
void Disable();
bool Enabled();
// Exactly one producer: public MissionControl's single btdrv MITM IPC thread.
void Observe(std::uint32_t command, std::uint64_t program, std::uint64_t process,
    const void *payload, std::size_t size);
// Exactly one future consumer; no logger/Main integration installed by this task.
bool Pop(::amazon_remote::IntentRecord &out);
void SnapshotInto(::amazon_remote::IntentCounters &out);
// Optional future BLE hook, before strict observer validation. Separate SPSC
// producer/queue; only first7 bytes of type6 events, never foreign MAC/name/AD.
void ObserveManagedHeader(std::uint32_t type, const void *event, std::size_t size, bool configured_address_hint = false);
bool PopManagedHeader(::amazon_remote::BleHeaderRecord &out);
void HeaderSnapshotInto(::amazon_remote::BleHeaderCounters &out);
}
