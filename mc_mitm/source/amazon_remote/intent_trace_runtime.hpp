// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "intent_trace.hpp"
#include "ble_header_trace.hpp"
namespace ams::mc::ble_intent_trace {
// Managed-observer integration: Main/Startup only, before MITM thread starts.
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
// Exactly one consumer: existing managed-observer maintenance/logging thread.
bool Pop(::amazon_remote::IntentRecord &out);
void SnapshotInto(::amazon_remote::IntentCounters &out);
// Existing BLE event tap, before strict observer validation. Separate SPSC
// producer/queue; first7 bytes of type6 and first8 bytes of type7 events only.
// No foreign MAC/name/AD. Filter completion action/result is not ownership proof.
// Existing power teardown disables this logger; it is not a suspend-spanning trace.
void ObserveManagedHeader(std::uint32_t type, const void *event, std::size_t size, bool configured_address_hint = false);
bool PopManagedHeader(::amazon_remote::BleHeaderRecord &out);
void HeaderSnapshotInto(::amazon_remote::BleHeaderCounters &out);
}
