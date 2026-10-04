// SPDX-License-Identifier: GPL-2.0-only
// One-shot native HDLS test. No boot flag, Bluetooth, target access or PSC.
#include "hdls_backend.hpp"
#include "native_hdls.hpp"
#include "launch_fix_sdk.hpp"

namespace { constinit armodule::NativeResult report{}; bool sm_open{},fs_open{},mounted{}; }
extern "C" {
u32 __nx_applet_type=AppletType_None;
u32 __nx_fs_num_sessions=1;
void __libnx_initheap() {
    extern void *fake_heap_start,*fake_heap_end;
    // Existing writable .bss storage, not a kernel heap expansion. aligned_alloc
    // gives the backend its own isolated page without consuming the stack.
    alignas(4096) static char arena[0x80000];
    fake_heap_start=arena; fake_heap_end=arena+sizeof arena;
    report.heap_result=0; // allocator-arena setup, NOT svcSetHeapSize success
}
void __appInit() {
    report.instance=armGetSystemTick();
    report.sm_result=smInitialize(); sm_open=report.sm_result==0; if (!sm_open) return;
    report.fs_result=fsInitialize(); fs_open=report.fs_result==0; if (!fs_open) return;
    report.mount_result=fsdevMountSdmc(); mounted=report.mount_result==0; if (!mounted) return;
    report.firmware_result=setsysInitialize(); if (report.firmware_result) return;
    SetSysFirmwareVersion version{}; report.firmware_result=setsysGetFirmwareVersion(&version); setsysExit();
    if (!report.firmware_result) hosversionSet(MAKEHOSVERSION(version.major,version.minor,version.micro));
}
void __appExit() {
    if (mounted) fsdevUnmountAll();
    if (fs_open) fsExit();
    if (sm_open) smExit();
}
}
namespace {
std::uint64_t Now() { return armTicksToNs(armGetSystemTick())/1000000; }
bool Absent(const char *path) { struct stat info{}; errno=0; return ::stat(path,&info)!=0 && errno==ENOENT; }
bool Save() {
    report.now_ms=Now(); report.checksum=armodule::NativeChecksum(report);
    return armodule::Publish(armodule::NativeResultTemp,armodule::NativeResultPath,report);
}
}
int main(int,char **) {
    using namespace armodule;
    if (!mounted) return 0;
    NativePermit permit{};
    // An explicit, fresh operator permit is consumed durably before any HID
    // service call. Existing or ambiguous evidence never enables a retry.
    if (!ReadExact(NativePermitPath,permit) || !ValidNativePermit(permit,Now()) ||
        !Absent(NativeUsedPath) || !Absent(NativeResultPath) || !Absent(NativeResultTemp) ||
        !FixSaveExclusive(NativeUsedPath,permit)) return 0;
    report.permit_issued_ms=permit.issued_ms; report.permit_consumed=1;
    report.stage=1; report.now_ms=Now(); report.checksum=NativeChecksum(report);
    if (!FixSaveExclusive(NativeResultPath,report)) return 0;
    if (report.heap_result || report.firmware_result || hosversionGet()!=MAKEHOSVERSION(22,5,0)) {
        report.stage=2; report.result=report.heap_result ? report.heap_result : report.firmware_result ? report.firmware_result : MAKERESULT(Module_Libnx,LibnxError_BadInput);
        Save(); return 0;
    }
    HdlsBackend backend; OwnedController controller(backend);
    report.result=NeutralControllerCheck(controller);
    report.trace=backend.Trace(report.instance,Now()); report.stage=3; Save();
    // Exit releases process resources, including any conservatively quarantined
    // page. No polling loop, registration, capture, radio change or reconnect.
    return 0;
}
