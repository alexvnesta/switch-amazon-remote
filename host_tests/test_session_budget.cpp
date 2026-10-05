// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/session_budget.hpp"
#include "../mc_mitm/source/amazon_remote/health_log_gate.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    using namespace amazon_remote;
    SessionBudget original,sustained(true);
    for (auto *b:{&original,&sustained}) {
        assert(b->Expired(100,99));
        assert(!b->Expired(100,90099));
        assert(b->Expired(100,90100));
        assert(!b->AdmitReady(100,90100));
        assert(b->AdmitReady(100,90099));
    }
    assert(original.Expired(100,90100));
    assert(!sustained.Expired(100,90100));
    assert(!sustained.Expired(100,1890098));
    assert(sustained.Expired(100,1890099));
    assert(!sustained.AdmitReady(100,1890099));
    assert(sustained.Expired(100,90098));
    sustained.Reset();assert(sustained.Expired(100,90100));
    SessionBudget zero(true);assert(zero.AdmitReady(0,0));
    assert(!zero.Expired(0,1799999) && zero.Expired(0,1800000));
    HealthLogGate gate;
    assert(gate.Due(0,1) && !gate.Due(0,1));
    unsigned messages=1;
    for (unsigned now=250;now<=1800000;now+=250) messages+=gate.Due(now,1);
    assert(messages==61);
    assert(gate.Due(1800001,2) && gate.Due(1800002,1));
    assert(gate.Due(3,1));
    gate.Reset();assert(gate.Due(4,1));
    std::puts("PASS unchanged90s setup, opt-in fixed30min Ready, exact expiry, late completion, nonrenewal, clocks/reset and health-log sampling. MODEL ONLY.");
}
