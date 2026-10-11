// SPDX-License-Identifier: GPL-2.0-only
// Fixed synthetic test entry points ONLY. Not linked into the Switch module.
// The private replay resolves synthetic_set_encryption to stored native code;
// ownership/time checks remain explicit harness boundaries, not runtime proof.
#include "../mc_mitm/source/amazon_remote/legacy_encryption_request.hpp"
#include <new>
namespace e=amazon_remote::bond::encryption;
extern "C" {
std::uint8_t synthetic_set_encryption(const std::uint8_t *,std::uint8_t,e::Callback,void *);
std::uint64_t synthetic_now();
bool synthetic_admit(const e::Target *);
bool synthetic_owns(const e::Target *);
bool synthetic_encrypted(const e::Target *);
}
struct Backend {
    std::uint64_t Now(){return synthetic_now();}
    bool Admit(const e::Target &t){return synthetic_admit(&t);}
    bool Owns(const e::Target &t){return synthetic_owns(&t);}
    bool Encrypted(const e::Target &t){return synthetic_encrypted(&t);}
    std::uint8_t SetEncryption(const std::uint8_t *p,std::uint8_t transport,e::Callback cb,void *r){
        return synthetic_set_encryption(p,transport,cb,r);
    }
};
using Client=e::Request<Backend>;
static_assert(sizeof(e::Target)==40 && offsetof(e::Target,hci_handle)==32);
static Backend backend;
alignas(Client) static std::byte storage[sizeof(Client)];
static Client *client;
extern "C" void synthetic_initialize(){client=new(storage) Client(backend);}
extern "C" unsigned synthetic_begin(){
    // Same fixed address as the existing native replay, never console input.
    e::Target target{{{6,5,4,3,2,1}},1,2,3,0x123,16,false};
    return unsigned(client->Begin(target));
}
extern "C" unsigned synthetic_poll(){return unsigned(client->Poll());}
extern "C" unsigned synthetic_ready(){return client->TakeReady();}
extern "C" unsigned synthetic_cancel(){client->Cancel();return unsigned(client->GetState());}
