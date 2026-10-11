// SPDX-License-Identifier: GPL-2.0-only
// Passive diagnostics only: no driver scan, GATT, bond, or HID-output ownership.
#pragma once
#ifdef ARMODULE_WAKE_A3
#include "central_bond_wire.hpp"
#endif
#include <cstddef>
#include <cstdint>

namespace ams::mc::amazon_observer {
#ifdef ARMODULE_WAKE_A3
bool ResumeSavedSession(std::uint64_t caller,const ::amazon_remote::central_bond::WakeRequest &request);
#endif
// Startup/Main thread only, before the Bluetooth producer starts.
void Initialize();
// Main thread only. Explicit file opt-in, finite boot session, no automatic rearm.
void Start();
void Poll();
#ifdef ARMODULE_WAKE_A3
// Main maintenance thread only. Handshake work is separate from generic polling.
void PollHandshake();
#endif
void Suspend(bool shutdown);
void PowerRequest(std::uint32_t state, std::uint32_t flags, std::uint32_t result);
void PowerAcknowledge(std::uint32_t result);
// Existing BLE event thread only. Bounded copy; no filesystem/IPC/heap/wait.
void Observe(std::uint32_t type, const void *event, std::size_t size);
// Read-only exact-target diagnostic snapshot for the foreground link test.
// No scan, filter, connection, pairing or output command is issued here.
bool ReadTargetSighting(void *out, std::size_t size);
// MC IPC thread: one-shot passive lease only. No radio or filesystem calls.
bool ArmTargetCapture();
void EndTargetCapture();
bool ReadCaptureStatus(void *out, std::size_t size);
bool ReadCaptureDiagnostics(void *out, std::size_t size);
// Retained, target-only connection callbacks, including errors. Evidence only.
bool ReadConnectionEvidence(void *out, std::size_t size);
// Existing CORE event thread only, before forwarding/name changes/PIN replies.
// Metadata only; never reads driver events independently or responds to them.
void ObserveCore(std::uint32_t type, const void *event, std::size_t size);
bool ReadCoreSecurityEvidence(void *out, std::size_t size);
// M1: one explicit nonce probe through the native queue, no radio operation.
bool BeginNativeMetadataProbe(std::uint32_t nonce);
void ObserveNativeMetadata(const void *event, std::size_t size);
bool ReadNativeMetadata(void *out, std::size_t size);
bool ReadBondProperties(void *out, std::size_t size);
}
