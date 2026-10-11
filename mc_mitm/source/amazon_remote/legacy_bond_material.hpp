// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace amazon_remote::bond::legacy {
// Private adapter primitive, not a public IPC or file format. No runtime caller
// exists yet. Only use after exact driver compatibility and target/connection
// ownership have been established. A build-ID match alone is insufficient.
// The audited C91... driver uses the PRE-Secure-Connections key union. In this
// ABI 0x08 is four-byte local encryption reproduction metadata, NOT a link key.
enum class Layout { Unsupported, AuditedC91Legacy };
enum class Kind : std::uint8_t {
    PeerEncryption=1, PeerIdentity=2, PeerSigning=4,
    LocalEncryption=8, LocalSigning=0x20
};
using Address = std::array<std::uint8_t, 6>;
inline constexpr std::size_t EventBytes=36, UnionBytes=28;

// Meaningful bytes, excluding native structure padding. The private adapter
// can give the native loader a fully initialized 28-byte union. Never retain
// unused bytes from the callback stack, even for a recognized key kind.
constexpr std::size_t MeaningfulBytes(std::uint8_t kind) {
    switch (static_cast<Kind>(kind)) {
        case Kind::PeerEncryption: return 28;
        case Kind::PeerIdentity: return 23;
        case Kind::PeerSigning: return 21;
        case Kind::LocalEncryption: return 4;
        case Kind::LocalSigning: return 7;
    }
    return 0;
}
constexpr bool Nonzero(const Address &address) {
    for (const auto byte:address) if (byte) return true;
    return false;
}
inline void Erase(std::span<std::uint8_t> bytes) {
    // Volatile stores prevent dead-store elimination. This does not erase the
    // caller's callback, stack/register copies, crash dumps or persistent media.
    volatile std::uint8_t *p=bytes.data();
    for (std::size_t i=0; i<bytes.size(); ++i) p[i]=0;
}

class Material {
    std::array<std::uint8_t, UnionBytes> bytes_{};
    std::uint8_t kind_{};
    friend bool Decode(Layout, const Address &, std::span<const std::uint8_t>, Material &);
public:
    Material() = default;
    ~Material() { Clear(); }
    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;
    Material(Material &&) = delete;
    Material &operator=(Material &&) = delete;
    void Clear() { Erase(bytes_); kind_=0; }
    bool Valid() const { return MeaningfulBytes(kind_)!=0; }
    std::uint8_t KeyKind() const { return kind_; }
    // Private store/native adapter only. Never include this span in diagnostics,
    // public IPC, SD config or crash annotations. It expires with this object.
    std::span<const std::uint8_t, UnionBytes> NativeUnion() const { return bytes_; }
};

inline bool Decode(Layout layout, const Address &expected_target,
                   std::span<const std::uint8_t> event, Material &out) {
    // Input and out must not alias. All errors erase the previous output so a
    // malformed new event cannot accidentally reuse a prior candidate.
    out.Clear();
    if (layout!=Layout::AuditedC91Legacy || !Nonzero(expected_target) ||
        event.size()!=EventBytes) return false;
    for (std::size_t i=0; i<expected_target.size(); ++i)
        if (event[i]!=expected_target[i]) return false;
    const auto length=MeaningfulBytes(event[6]);
    if (!length) return false;
    // Byte7 is native alignment padding. The union begins at byte8. Preserve
    // byte order; do not reinterpret as a C++ struct or infer negotiated flags.
    for (std::size_t i=0; i<length; ++i) out.bytes_[i]=event[8+i];
    out.kind_=event[6];
    return true;
}
// Decoded means layout/target framing only, not valid cryptographic material,
// negotiated bonding, a complete key set, MITM protection, durable storage or
// successful native reload. Those checks belong to the transaction adapter.
// In particular LENC contains DIV/size/security, not an LTK. Reproduction may
// depend on native local roots; do not regenerate or replace global roots.
}
