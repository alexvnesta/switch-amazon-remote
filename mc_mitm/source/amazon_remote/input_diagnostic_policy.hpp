// Independent diagnostic authorization whitelist. GPL-2.0-only.
#pragma once
#include "hogp_client.hpp"
namespace amazon_remote {
// Connection/generation/current target are checked separately by the transport.
inline bool IsInputDiagnosticRequest(const Request &r, Phase phase,
        bool fingerprint, bool security_read, bool inputs_loaded) {
    if (!fingerprint || r.authentication != Authentication::NoMitm || !r.service.primary || r.service.uuid != 0x1812) return false;
    if (phase == Phase::SecurityRead)
        return !security_read && !inputs_loaded && IsFingerprintRead(r) && r.characteristic.uuid == 0x2a4b;
    if (!security_read || !inputs_loaded || r.characteristic.uuid != 0x2a4d) return false;
    if (phase == Phase::References)
        return r.operation == Operation::ReadDescriptor && r.descriptor.uuid == 0x2908;
    if (phase != Phase::Subscribe) return false;
    return r.operation == Operation::RegisterNotification ||
        (r.operation == Operation::WriteDescriptor && r.descriptor.uuid == 0x2902 && r.size == 2 && r.value[0] == 1 && r.value[1] == 0);
}
}
