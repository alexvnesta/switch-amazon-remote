// SPDX-License-Identifier: GPL-2.0-only
#include "../connection_capture.hpp"
#include <cassert>
#include <string>
#include <vector>
std::vector<std::string> lines;
void Log(const char *line) { lines.emplace_back(line); }
int main() {
    using namespace amazon_remote;armodule::ConnectionCapture capture;
    ConnectionEvidenceWire wire{};assert(capture.Accept(wire,1000,Log) && lines.empty());
    wire.target_count=1;wire.events[0]={1000,0xdeadbeef,4,255,9,0x123e,0};
    assert(!capture.Accept(wire,999,Log) && lines.empty());
    assert(capture.Accept(wire,1000,Log) && lines.size()==1);
    assert(lines[0].find("deadbeef")!=std::string::npos && lines[0].find("123e")!=std::string::npos);
    assert(lines[0].find("request_correlation_unproven=1")!=std::string::npos);
    assert(capture.Accept(wire,1001,Log) && lines.size()==1);
    auto drift=wire;drift.events[0].reason=0;assert(!capture.Accept(drift,1001,Log) && lines.size()==1);
    drift=ConnectionEvidenceWire{};assert(!capture.Accept(drift,1001,Log));
    for (unsigned i=1;i<8;++i) wire.events[i]={1000+i,i,4,2,9,0,0};
    wire.target_count=8;wire.overflow=3;
    assert(capture.Accept(wire,1007,Log) && lines.size()==9);
    drift=wire;--drift.overflow;assert(!capture.Accept(drift,1007,Log));
    assert(capture.Accept(wire,1008,Log) && lines.size()==9);
    puts("PASS connection capture consumer: append-only coherent evidence, future/rewrite rejection and explicit overflow; no connection authorization");
}
