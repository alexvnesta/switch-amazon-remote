// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Real host SHA-256, not a fake digest. Production uses libnx's native routine.
#include <cassert>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif
inline void sha256CalculateHash(void *out,const void *in,std::size_t size) {
#ifdef __APPLE__
    assert(size<=0xffffffffu);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA256(in,static_cast<CC_LONG>(size),static_cast<unsigned char*>(out));
#pragma clang diagnostic pop
#else
    SHA256(static_cast<const unsigned char*>(in),size,static_cast<unsigned char*>(out));
#endif
}
