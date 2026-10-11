// SPDX-License-Identifier: GPL-2.0-only
#include "../controller.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
struct Pad {
    bool present{};unsigned attaches{},queries{},sets{},detaches{},releases{},closes{};
    unsigned fail_attach{},fail_set{},fail_query{};bool vanish_on_attach{};
    std::vector<std::uint64_t> writes;
    unsigned Open() { return 0; }
    unsigned AttachSession() { return 0; }
    unsigned AttachDevice() { ++attaches;if (fail_attach==attaches) return 42;present=!vanish_on_attach;return 0; }
    unsigned Set(std::uint64_t b) { ++sets;writes.push_back(b);if (fail_set==sets || !present) return 0x1c24ca;return 0; }
    unsigned CheckOwnedDevice(bool &p) { ++queries;p=false;if (fail_query==queries) return 47;p=present;return 0; }
    unsigned DetachDevice() { ++detaches;const auto rc=present?0u:0x1c24cau;present=false;return rc; }
    unsigned ReleaseSession() { ++releases;return 0; }
    void Close() { ++closes; }
};
int main() {
    using namespace amazon_remote;
    // Reproduce the 53-second idle gap. Query without injecting extra inputs.
    for (unsigned removed_at:{250u,1000u,53000u}) {
        Pad b;armodule::OwnedController c(b);assert(!c.Start());
        for (unsigned now=100;now<=54100;now+=20) {
            if (now>=removed_at && b.attaches==1) b.present=false;
            assert(!c.UpdateMaintained(0,now));
        }
        assert(b.attaches==2 && c.Recovered() && !c.WaitingRelease());
        assert(b.queries<=225 && b.writes.size()==2); // neutral only, no synthetic repeat
        assert(!c.UpdateMaintained(Down,54120) && b.writes.back()==(1ull<<15));
        assert(!c.UpdateMaintained(0,54140) && b.writes.back()==0);
        assert(!c.Stop() && b.detaches==1 && b.releases==1 && b.closes==1);
    }
    // Removal between periodic check and button press: forced pre-write query.
    { Pad b;armodule::OwnedController c(b);assert(!c.Start());
      assert(!c.UpdateMaintained(0,100));b.present=false;
      assert(!c.UpdateMaintained(Down,120) && c.WaitingRelease());
      assert(b.writes.back()==0 && b.attaches==2);
      assert(!c.UpdateMaintained(Down,140) && b.writes.back()==0);
      assert(!c.UpdateMaintained(Confirm,160) && b.writes.back()==0);
      assert(!c.UpdateMaintained(0,180) && !c.WaitingRelease());
      assert(!c.UpdateMaintained(Down,200) && b.writes.back()==(1ull<<15));assert(!c.Stop()); }
    // A key held when the handle disappears is neutralized, never replayed.
    for(std::uint32_t held:{std::uint32_t(Confirm),std::uint32_t(Home),std::uint32_t(Confirm|Home)}) {
      Pad b;armodule::OwnedController c(b);assert(!c.Start());
      assert(!c.UpdateMaintained(held,100) && b.writes.back()==armodule::MapButtons(held));b.present=false;
      assert(!c.UpdateMaintained(held,360) && c.WaitingRelease() && b.writes.back()==0);
      assert(!c.UpdateMaintained(held,380) && b.writes.back()==0);
      assert(!c.UpdateMaintained(0,400));
      assert(!c.UpdateMaintained(Home,420) && b.writes.back()==(1ull<<18));
      assert(!c.Stop() && b.writes.back()==0); }
    // Second disappearance stops; zero writes/detaches to confirmed stale handle.
    { Pad b;armodule::OwnedController c(b);assert(!c.Start());
      b.present=false;assert(!c.UpdateMaintained(0,100));
      const auto sets=b.sets;b.present=false;
      assert(c.UpdateMaintained(Down,120) && !c.Attached());
      assert(b.attaches==2 && b.sets==sets && !b.detaches && b.releases==1);
      assert(c.Start() && c.UpdateMaintained(0,400) && b.attaches==2); }
    // Real captured Set error is ambiguous: never infer disappearance/reattach.
    { Pad b;armodule::OwnedController c(b);assert(!c.Start());b.fail_set=2;
      assert(c.UpdateMaintained(Down,100)==0x1c24ca && b.attaches==1 && !c.Recovered());
      assert(b.writes.back()==0 && b.detaches==1 && b.releases==1);
      assert(c.UpdateMaintained(Down,120) && c.Start()); }
    // Query/open-attachment/first neutral/new-presence failures stop, no loop.
    for (unsigned failure=0;failure<4;++failure) {
        Pad b;armodule::OwnedController c(b);assert(!c.Start());b.present=false;
        if (failure==0) b.fail_query=1;
        if (failure==1) b.fail_attach=2;
        if (failure==2) b.fail_set=2;
        if (failure==3) b.fail_query=2;
        assert(c.UpdateMaintained(Down,100));const auto attaches=b.attaches;
        assert(!c.Attached() && b.releases==1 && b.closes==1);
        assert(c.UpdateMaintained(Down,120) && c.Start() && b.attaches==attaches);
    }
    { Pad b;armodule::OwnedController c(b);assert(!c.Start());
      b.present=false;b.vanish_on_attach=true;
      assert(c.UpdateMaintained(0,100) && b.attaches==2 && !b.detaches); }
    // Clock rollback and zero clock fail closed, no additional attachment.
    for (unsigned now:{0u,99u}) {
        Pad b;armodule::OwnedController c(b);assert(!c.Start());
        assert(!c.UpdateMaintained(0,100));assert(c.UpdateMaintained(0,now));assert(b.attaches==1);
    }
#ifdef ARMODULE_WAKE_A3
    { Pad b;armodule::OwnedController c(b);
      for(unsigned session=0;session<8;++session){
          assert(c.PrepareNextSession() && !c.Start() && !c.PrepareNextSession());
          const auto button=session%2?Home:Down;
          assert(!c.UpdateMaintained(button,1000+session*1000));
          assert(c.LastAcknowledgedButtons()==armodule::MapButtons(button));
          assert(!c.Stop() && !c.LastAcknowledgedButtons() && !c.Attached());
      }
      assert(b.attaches==8 && b.detaches==8 && b.releases==8 && b.closes==8);
    }
    { Pad b;armodule::OwnedController c(b);assert(!c.Start());b.fail_set=2;
      assert(c.UpdateMaintained(Down,100) && !c.PrepareNextSession()); }
#endif
    std::puts("PASS owned-HDLS lifetime: long idle, periodic/pre-write presence, one neutral replacement, release gate, second disappearance, ambiguous Set failure, every recovery failure and clock guards. MODEL ONLY.");
}
