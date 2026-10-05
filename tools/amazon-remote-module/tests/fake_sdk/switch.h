// SPDX-License-Identifier: GPL-2.0-only
// Minimal host model ONLY for testing the actual HDLS adapter's requests.
// Not an SDK ABI/layout or service-permission proof; the real SDK build is separate.
#pragma once
#include <cstddef>
#include <cstdint>
using Result = std::uint32_t;
inline constexpr unsigned Module_Libnx=345,LibnxError_OutOfMemory=2,LibnxError_AlreadyInitialized=7;
#define MAKERESULT(module,description) (((module)&0x1ff)|(((description)&0x1fff)<<9))
#define KERNELRESULT(description) MAKERESULT(1,KernelError_##description)
inline constexpr unsigned KernelError_InvalidMemoryState=106;
inline constexpr std::uint64_t HidNpadButton_A=1ull<<0, HidNpadButton_B=1ull<<1,
    HidNpadButton_Plus=1ull<<10, HidNpadButton_Left=1ull<<12, HidNpadButton_Up=1ull<<13,
    HidNpadButton_Right=1ull<<14, HidNpadButton_Down=1ull<<15, HiddbgNpadButton_Home=1ull<<18;
inline constexpr unsigned HidDeviceType_FullKey15=3, HidNpadInterfaceType_USB=1;
struct HiddbgHdlsSessionId { std::uint64_t id{}; };
struct HiddbgHdlsHandle { std::uint64_t handle{}; };
struct HiddbgHdlsDeviceInfo {
    unsigned deviceType{}, npadInterfaceType{}, singleColorBody{}, singleColorButtons{};
};
struct HiddbgHdlsState {
    std::uint64_t buttons{};
    unsigned battery_level{}, flags{};
    struct { int x{}, y{}; } analog_stick_l{}, analog_stick_r{};
};
Result hiddbgInitialize();
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId *, void *, std::size_t);
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle *, const HiddbgHdlsDeviceInfo *);
Result hiddbgSetHdlsState(HiddbgHdlsHandle, const HiddbgHdlsState *);
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle);
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId);
void hiddbgExit();
using s64 = std::int64_t;
struct FsFileSystem {};
struct FsFile {};
inline constexpr unsigned FsOpenMode_Read=1,FsOpenMode_Write = 2, FsWriteOption_Flush = 1;
Result fsFsOpenFile(FsFileSystem *, const char *, unsigned, FsFile *);
Result fsFsCreateFile(FsFileSystem *, const char *, s64, unsigned);
Result fsFileGetSize(FsFile *, s64 *);
Result fsFileWrite(FsFile *, s64, const void *, std::uint64_t, unsigned);
Result fsFileRead(FsFile *,s64,void *,std::uint64_t,unsigned,std::uint64_t *);
void fsFileClose(FsFile *);
using u64 = std::uint64_t;
using u8 = std::uint8_t;
inline bool R_SUCCEEDED(Result result) { return !result; }
struct Sha256Context {};
void sha256ContextCreate(Sha256Context *);
void sha256ContextUpdate(Sha256Context *, const void *, std::size_t);
void sha256ContextGetHash(Sha256Context *, void *);
u64 armGetSystemTick();
u64 armTicksToNs(u64);
struct NcmProgramLocation { u64 program_id{}; u8 storageID{}; u8 reserved[7]{}; };
inline constexpr unsigned NcmStorageId_None = 0, PmLaunchFlag_None = 0;
Result pmdmntInitialize();
Result pmdmntGetProcessId(u64 *, u64);
void pmdmntExit();
Result pmshellInitialize();
Result pmshellLaunchProgram(unsigned, const NcmProgramLocation *, u64 *);
void pmshellExit();
using u32 = std::uint32_t;
using Handle = std::uint32_t;
inline constexpr Handle INVALID_HANDLE = 0;
enum ProcessInfoType { ProcessInfoType_ProcessState = 0 };
enum { SfOutHandleAttr_HipcCopy = 1 };
struct Service {};
struct SfDispatchParams {
    struct { unsigned attr0{}; } out_handle_attrs;
    Handle *out_handles{};
};
Service *pmdmntGetServiceSession();
Result serviceDispatchImpl(Service *, u32, const void *, u32, void *, u32, SfDispatchParams);
Result pmdmntGetJitDebugProcessIdList(u32 *, u64 *, std::size_t);
bool envIsSyscallHinted(unsigned);
Result svcGetProcessInfo(s64 *, Handle, ProcessInfoType);
Result svcCloseHandle(Handle);
struct MemoryInfo { u64 addr{},size{}; u32 type{},attr{},perm{}; };
inline constexpr unsigned MemType_CodeStatic=3,MemType_CodeMutable=4,Perm_R=1,Perm_X=4;
inline constexpr unsigned MemType_Heap=5,Perm_Rw=3;
Result svcQueryMemory(MemoryInfo *,u32 *,u64);
Result svcSetHeapSize(void **,u64);
struct VirtmemReservation {};
void virtmemLock();
void virtmemUnlock();
void *virtmemFindCodeMemory(std::size_t,std::size_t);
VirtmemReservation *virtmemAddReservation(void *,std::size_t);
Result svcQueryProcessMemory(MemoryInfo *,u32 *,Handle,u64);
Result svcMapProcessMemory(void *,Handle,u64,u64);
Result svcUnmapProcessMemory(void *,Handle,u64,u64);
