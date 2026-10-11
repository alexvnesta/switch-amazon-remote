// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace amazon_remote {
// G1: the companion acknowledges PSC transitions on the same main thread that
// runs its maintenance storage work (one-shot diagnostics that save once the
// capture lease ends). Storage started mid-sleep can block the next
// acknowledgement and stall system sleep. Any non-FullAwake request, or any
// failed request, closes the gate before power handlers run. Only a successful
// acknowledgement of a successful FullAwake request reopens it. Main thread only.
class StorageGate {
public:
    void Request(std::uint32_t state,std::uint32_t result) {
        requested_=state;request_ok_=!result;
        if(state!=0 || result)open_=false;
    }
    void Acknowledge(std::uint32_t result) {
        if(result)open_=false;
        else if(request_ok_ && requested_==0)open_=true;
    }
    bool Open()const{return open_;}
private:
    std::uint32_t requested_{};
    bool open_{true},request_ok_{true};
};
}
