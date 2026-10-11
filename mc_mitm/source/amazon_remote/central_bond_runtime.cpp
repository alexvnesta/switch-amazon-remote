// SPDX-License-Identifier: GPL-2.0-only
// B1 bounded central-only trial. Keys never leave the privileged runtime except
// to the fixed native bridge and the project's private Device save in USER.
#include <stratosphere.hpp>
#include <switch.h>
#include "central_bond_runtime.hpp"
#include "bond_record.hpp"
#include "legacy_encryption_request.hpp"
#include "capture_lease.hpp"
#include "managed_observer_runtime.hpp"
#include "hci_status_trace.hpp"
#include <cstring>
#ifdef ARMODULE_WAKE_A3
#include "wake_session.hpp"
#include <optional>
#endif

namespace ams::mc::central_bond_runtime {
namespace {
using ::Result;
namespace cb=::amazon_remote::central_bond;
namespace bond=::amazon_remote::bond;
namespace rec=bond::record;
using cb::Phase; using cb::Store;
constexpr std::uint32_t Profile=0x314e4543; // CEN1: central PENC projection only.
os::SdkMutex mutex;
cb::Status status;
cb::PeerStatus peer_status;
cb::HciTrace hci_trace;
bool peer_used{};
std::uint64_t peer_pid{};
rec::Binding binding;
rec::Buffer previous,next;
bond::legacy::Material candidate;
bool configured{},loaded{},suspended{},attempted{},alive{},completion{},hello{};
bool idle_power_pause{};
bool provisioning_required{};
std::uint32_t connection{},link_generation{},owned_generation{};
std::uint16_t handle=0xffff;
std::uint64_t started{},pid{};
Result last_result{};
#ifdef ARMODULE_WAKE_A3
::amazon_remote::WakeSession wake;
bool fs_open{},hello_sent{};
std::array<std::uint32_t,::amazon_remote::WakeSession::Capacity> peer_nonces{},bond_nonces{};
unsigned session_index{};
#endif
bond::encryption::Callback callback{};void *reference{};
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
Result Invalid(){return MAKERESULT(Module_Libnx,LibnxError_BadInput);}
bool Call(Result rc){last_result=rc;return R_SUCCEEDED(rc);}
bool Owned(){return alive && connection==status.connection && link_generation==owned_generation && !suspended;}
void Fail(Result rc){status.phase=Phase::Failed;status.result=rc?rc:Invalid();candidate.Clear();next.Clear();}
bool Owner(std::uint64_t caller){u64 program{};return caller && R_SUCCEEDED(pminfoGetProgramId(&program,caller)) && program==bond::save_io::OwnerProgramId;}
bool PeerWindow(std::uint64_t deadline){
    ::amazon_remote::CaptureLeaseWire w{};
    const auto now=Now();
    return amazon_observer::ReadCaptureStatus(&w,sizeof w) && w.magic==::amazon_remote::CaptureLeaseMagic &&
        w.version==::amazon_remote::CaptureLeaseVersion && !w.reserved &&
        w.state==std::uint32_t(::amazon_remote::CaptureLeaseState::Active) && w.deadline_boot_ms==deadline &&
        w.now_boot_ms<=now && now-w.now_boot_ms<=1000 && deadline>now && deadline-now<=60000;
}
void PeerFail(Result rc){peer_status.phase=cb::PeerPhase::Failed;peer_status.result=rc?rc:Invalid();}
bool SendPeer(){
    std::array<std::uint32_t,11> wire{};wire[0]=10;wire[1]=cb::Magic;wire[2]=peer_status.nonce;
    BtdrvAddress zero{};return Call(btdrvWriteHidData2(zero,wire.data(),sizeof wire));
}
// Runs on the maintenance thread only. A missing save is not silently treated
// as valid empty storage: only exact filtered absence allows provisioning later.
bool Inventory(bool &absent) {
    absent=false;FsSaveDataFilter filter{};
    filter.filter_by_application_id=filter.filter_by_save_data_type=true;
    filter.filter_by_user_id=filter.filter_by_system_save_data_id=filter.filter_by_index=true;
    filter.save_data_rank=FsSaveDataRank_Primary;filter.attr=bond::save_io::Attribute();
    FsSaveDataInfoReader reader{};
    if(!Call(fsOpenSaveDataInfoReaderWithFilter(&reader,FsSaveDataSpaceId_User,&filter)))return false;
    ON_SCOPE_EXIT{fsSaveDataInfoReaderClose(&reader);};
    FsSaveDataInfo rows[2]{};s64 count{};
    if(!Call(fsSaveDataInfoReaderRead(&reader,rows,2,&count)))return false;
    if(count==0){absent=true;return true;}
    if(count!=1 || !bond::save_io::SameInfo(rows[0]))return false;
    return Call(fsSaveDataInfoReaderRead(&reader,rows,2,&count)) && count==0;
}
bool Load() {
#ifdef ARMODULE_WAKE_A3
    if(!fs_open){if(!Call(fsInitialize()))return false;fs_open=true;}
#else
    if(!Call(fsInitialize()))return false; // Separate libnx FS session, private I/O.
#endif
    BtdrvAdapterProperty property{};
    if(!Call(btdrvGetAdapterProperty(BtdrvAdapterPropertyType_Address,&property)))return false;
    std::memcpy(binding.local.data(),property.data,6);
    binding.profile=Profile;binding.local_type=binding.peer_type=0;
    // This profile intentionally does NOT bind local root lifetime. Central
    // action1 uses peer PENC only. Hash is a public-controller namespace, not a
    // key, MAC, complete key-distribution claim or secret-root attestation.
    std::array<std::uint8_t,16> ns{'A','R','-','C','E','N','1',0};
    std::copy(binding.local.begin(),binding.local.end(),ns.begin()+8);
    sha256CalculateHash(binding.controller_context.data(),ns.data(),ns.size());
    if(!rec::Valid(binding))return false;
    bool absent{};if(!Inventory(absent))return false;
    if(absent){provisioning_required=true;status.store=Store::Empty;return rec::Empty(binding,previous);}
    bond::save_io::DeviceSlot slot;rec::Metadata meta{};
    if(!slot.ReadCommitted(previous.Write()) || !rec::Inspect(previous.Read(),binding,16,meta) ||
       (meta.occupied && (meta.mask!=1 || meta.mitm)))return false;
    status.store=meta.occupied?Store::Saved:Store::Empty;status.generation=meta.generation;
    return true;
}
bool Provision() {
    if(!provisioning_required)return true;
    bool absent{};if(!Inventory(absent) || !absent || !Owned())return false;
    FsSaveDataCreationInfo info{};info.save_data_size=0x10000;info.journal_size=0x10000;
    info.available_size=0x4000; // Native creation granularity used by libnx helpers.
    info.owner_id=bond::save_io::OwnerProgramId;info.save_data_space_id=FsSaveDataSpaceId_User;
    FsSaveDataMetaInfo meta{};auto attr=bond::save_io::Attribute();
    if(!Call(fsCreateSaveDataFileSystem(&attr,&info,&meta)))return false;
    FsFileSystem fs{};if(!Call(fsOpenSaveDataFileSystem(&fs,FsSaveDataSpaceId_User,&attr)))return false;
    ON_SCOPE_EXIT{fsFsClose(&fs);};
    if(!Owned() || !Call(fsFsCreateFile(&fs,bond::save_io::SlotPath,512,0)))return false;
    FsFile file{};if(!Call(fsFsOpenFile(&fs,bond::save_io::SlotPath,FsOpenMode_Write,&file)))return false;
    const bool written=Call(fsFileWrite(&file,0,previous.Read().data(),512,FsWriteOption_None)) && Call(fsFileFlush(&file));
    fsFileClose(&file);
    if(!written || !Owned() || !Call(fsFsCommit(&fs)))return false;
    provisioning_required=false;return true; // Replace reopens/compares all512 bytes.
}
bool Send(std::uint32_t kind) {
    struct Wire {std::uint32_t kind,magic,nonce;std::uint8_t penc[28];std::uint16_t handle,reserved;} wire{};
    static_assert(sizeof wire==44);
    wire.kind=kind;wire.magic=cb::Magic;wire.nonce=status.nonce;wire.handle=handle;
    if(kind==7)std::memcpy(wire.penc,previous.Read().data()+rec::SlotsOffset+4,28);
    BtdrvAddress zero{};const auto rc=btdrvWriteHidData2(zero,&wire,sizeof wire);
    bond::legacy::Erase(std::span(reinterpret_cast<std::uint8_t*>(&wire),sizeof wire));
    return Call(rc);
}
struct Backend {
    std::uint64_t Now(){return ::ams::mc::central_bond_runtime::Now();}
    bool Owns(const bond::encryption::Target &t){return Owned() && t.connection_epoch==owned_generation && t.hci_handle==handle;}
    bool Admit(const bond::encryption::Target &t){return Owns(t) && hello && handle<=0xeff;}
    bool Encrypted(const bond::encryption::Target &t){return Owns(t) && completion && status.encrypted && status.key_bytes==16 && status.native_status==0;}
    std::uint8_t SetEncryption(const std::uint8_t *,std::uint8_t,bond::encryption::Callback cb,void *ref){
        callback=cb;reference=ref;
        // Accepted queued request is pending, NOT native encryption completion.
        return Send(status.store==Store::Saved?7:9)?1:3;
    }
} backend;
#ifdef ARMODULE_WAKE_A3
// Never relocate or reuse a callback context, including after cancellation.
std::array<std::optional<bond::encryption::Request<Backend>>,::amazon_remote::WakeSession::Capacity> requests;
bond::encryption::Request<Backend> &Encryption(){
    if(!requests[session_index])requests[session_index].emplace(backend);
    return *requests[session_index];
}
#else
bond::encryption::Request<Backend> encryption(backend); // Never reset/free/rearm.
bond::encryption::Request<Backend> &Encryption(){return encryption;}
#endif
}
void Initialize(const std::array<std::uint8_t,6> &peer){std::scoped_lock lock(mutex);binding.peer=peer;configured=bond::legacy::Nonzero(peer);
#ifdef ARMODULE_NATIVE_HCI_C2
    hci_trace.supported=1;
#endif
}
bool ReadHciTrace(void *out,std::size_t size){
    if(!out || size!=sizeof hci_trace)return false;
    std::scoped_lock lock(mutex);std::memcpy(out,&hci_trace,sizeof hci_trace);return true;
}
bool ReadStatus(void *out,std::size_t size){
    if(!out || size!=sizeof status)return false;
    std::scoped_lock lock(mutex);std::memcpy(out,&status,sizeof status);return true;
}
bool ReadPeerStatus(void *out,std::size_t size){
    if(!out || size!=sizeof peer_status)return false;
    std::scoped_lock lock(mutex);
    if(peer_status.phase==cb::PeerPhase::Ready && !attempted && !PeerWindow(peer_status.capture_deadline_ms))PeerFail(Invalid());
    std::memcpy(out,&peer_status,sizeof peer_status);return true;
}
bool PreparePeer(std::uint64_t caller,const cb::PreparePeer &r){
    if(!Owner(caller))return false;
    std::scoped_lock lock(mutex);
    if(!configured || !loaded || suspended || attempted || alive || peer_used || !r.nonce || r.reserved ||
       status.store!=Store::Saved || !status.generation || !PeerWindow(r.capture_deadline_ms))return false;
#ifdef ARMODULE_WAKE_A3
    if(std::find(peer_nonces.begin(),peer_nonces.end(),r.nonce)!=peer_nonces.end())return false;
    peer_nonces[session_index]=r.nonce;
#endif
    peer_used=true;peer_pid=caller;peer_status.nonce=r.nonce;peer_status.started_ms=Now();
    peer_status.capture_deadline_ms=r.capture_deadline_ms;peer_status.generation=status.generation;
    peer_status.phase=cb::PeerPhase::Queued;return true;
}
bool Begin(std::uint64_t caller,const cb::Begin &r){
    if(!Owner(caller))return false;
    std::scoped_lock lock(mutex);
#ifdef ARMODULE_NATIVE_HCI_C2
    if(status.store!=Store::Saved)return false; // native diagnostic has no enrollment tap
#endif
    if(!configured || !loaded || suspended || attempted || !r.nonce || !r.owner_epoch ||
       !alive || r.connection!=connection || (status.store!=Store::Empty && status.store!=Store::Saved))return false;
#ifdef ARMODULE_WAKE_A3
    if(std::find(bond_nonces.begin(),bond_nonces.end(),r.nonce)!=bond_nonces.end())return false;
    bond_nonces[session_index]=r.nonce;
#endif
    attempted=true;pid=caller;status.nonce=r.nonce;status.connection=r.connection;status.owner_epoch=r.owner_epoch;
    owned_generation=link_generation;started=Now();status.phase=Phase::Hello;return true;
}
void Cancel(std::uint64_t caller){
    if(!Owner(caller))return;
    std::scoped_lock lock(mutex);
#ifdef ARMODULE_WAKE_A3
    if(caller==pid || caller==peer_pid)wake.Stop();
#endif
    if(peer_used && caller==peer_pid)peer_status.phase=cb::PeerPhase::Cancelled;
    if(caller!=pid)return;
    Encryption().Cancel();status.phase=Phase::Cancelled;candidate.Clear();next.Clear();
}
void Suspend(){std::scoped_lock lock(mutex);suspended=true;idle_power_pause=false;Encryption().Cancel();candidate.Clear();next.Clear();if(attempted)status.phase=Phase::Cancelled;if(peer_used)peer_status.phase=cb::PeerPhase::Cancelled;
#ifdef ARMODULE_WAKE_A3
    wake.Stop();
#endif
}
#ifdef ARMODULE_WAKE_A3
bool PauseSessionForPower(){
    std::scoped_lock lock(mutex);
    if(!configured || !loaded || status.store!=Store::Saved || !attempted || !wake.Sleep())return false;
    suspended=true;Encryption().Cancel();candidate.Clear();previous.Clear();next.Clear();return true;
}
bool ResumeSessionAfterPower(){
    std::scoped_lock lock(mutex);
    if(!suspended || !wake.Awake())return false;
    // Keep session operations suspended until exact-owner absence admission.
    return true;
}
bool ReadWakeStatus(void *out,std::size_t size){
    if(!out || size!=sizeof(cb::WakeStatus))return false;
    std::scoped_lock lock(mutex);cb::WakeStatus w;
    w.serial=wake.Serial();w.ready=wake.Ready(!alive && link_generation>owned_generation);
    w.remaining=wake.Remaining();w.nonce=status.nonce;w.connection=status.connection;w.owner_epoch=status.owner_epoch;
    std::memcpy(out,&w,sizeof w);return true;
}
bool ResumeSavedSession(std::uint64_t caller,const cb::WakeRequest &r){
    if(!Owner(caller))return false;
    std::scoped_lock lock(mutex);
    if(caller!=pid || r.reserved || session_index+1>=requests.size() || !suspended ||
       status.store!=Store::Saved || !wake.Consume(r.serial,r.nonce,r.connection,r.owner_epoch,!alive && link_generation>owned_generation))return false;
    Encryption().Cancel();++session_index;
    previous.Clear();candidate.Clear();next.Clear();callback=nullptr;reference=nullptr;
    status={};peer_status={};hci_trace={};hci_trace.supported=1;
    peer_used=loaded=suspended=attempted=completion=hello=hello_sent=false;
    idle_power_pause=provisioning_required=false;pid=peer_pid=0;handle=0xffff;owned_generation=0;started=0;last_result=0;
    return true; // Maintenance reloads the SAME committed saved record, never enrolls.
}
#endif
bool PauseUnusedForPower(){
    std::scoped_lock lock(mutex);
    if(attempted || alive || peer_used || (suspended && !idle_power_pause))return false;
    suspended=idle_power_pause=true;return true;
}
bool ResumeUnusedAfterPower(){
    std::scoped_lock lock(mutex);
    if(!idle_power_pause || attempted || alive || peer_used)return false;
    suspended=idle_power_pause=false;return true;
}
void ObserveBle(std::uint32_t type,const void *data,std::size_t size){
    if(type!=4 || !data || size<20)return;
    const auto *p=static_cast<const std::uint8_t*>(data);
    std::scoped_lock lock(mutex);
    if(!configured || !std::equal(binding.peer.begin(),binding.peer.end(),p+12))return;
    std::uint32_t result{},id{};std::memcpy(&result,p,4);std::memcpy(&id,p+8,4);
    // Observed native status0=connected, status2=disconnected. Unknown is not live.
    ++link_generation;connection=id;alive=!result && p[4]==0;
    if(peer_status.phase==cb::PeerPhase::Queued || peer_status.phase==cb::PeerPhase::Waiting)PeerFail(Invalid());
    if(attempted && !Owned() && (status.phase==Phase::Hello || status.phase==Phase::Encrypting || status.phase==Phase::Saving)){
        Encryption().Cancel();Fail(Invalid());
    }
}
void ConsumePrivate(void *data,std::size_t size){
    if(!data)return;
    auto bytes=std::span(static_cast<std::uint8_t*>(data),size);
    ON_SCOPE_EXIT{bond::legacy::Erase(bytes);};
    std::scoped_lock lock(mutex);
#ifdef ARMODULE_NATIVE_HCI_C2
    if(size>=64 && bytes[10]==6){
        const bool active=configured && loaded && peer_used && !suspended &&
            status.store==Store::Saved && peer_status.phase==cb::PeerPhase::Ready &&
            PeerWindow(peer_status.capture_deadline_ms);
        (void)cb::CaptureHciStatus(hci_trace,bytes,binding.peer,peer_status.nonce,
                                 peer_status.started_ms,Now(),active);
        return;
    }
#endif
    if(size>=64 && configured && peer_used && !suspended && !alive &&
       peer_status.phase==cb::PeerPhase::Waiting && Now()>=peer_status.started_ms &&
       Now()-peer_status.started_ms<3000 && PeerWindow(peer_status.capture_deadline_ms)){
        std::uint32_t magic{},nonce{};std::memcpy(&magic,bytes.data(),4);std::memcpy(&nonce,bytes.data()+12,4);
        if(magic==cb::Magic && nonce==peer_status.nonce && bytes[10]==5 &&
           std::equal(binding.peer.begin(),binding.peer.end(),bytes.begin()+4)){
#ifdef ARMODULE_NATIVE_HCI_C2
            if(bytes[47]!=2){PeerFail(Invalid());return;} // fail before connect on old native bridge
#endif
            if(!std::all_of(bytes.begin()+16,bytes.begin()+44,[](auto v){return v==0;}) || bytes[46]>1){PeerFail(Invalid());return;}
            peer_status.existed=bytes[46];peer_status.device_type=bytes[44];peer_status.address_type=bytes[45];
            if(bytes[11] || bytes[44]!=2 || bytes[45]!=0){PeerFail(bytes[11]?bytes[11]:Invalid());return;}
            peer_status.phase=cb::PeerPhase::Ready;return;
        }
    }
    if(size<64 || !configured || !attempted || !Owned() || Now()-started>=30000 ||
       (status.phase!=Phase::Hello && status.phase!=Phase::Encrypting))return;
    std::uint32_t magic{},nonce{};std::memcpy(&magic,bytes.data(),4);std::memcpy(&nonce,bytes.data()+12,4);
    if(magic!=cb::Magic || nonce!=status.nonce || !std::equal(binding.peer.begin(),binding.peer.end(),bytes.begin()+4))return;
    const auto kind=bytes[10];
    if(kind==0 && status.phase==Phase::Hello && bytes[11]==1 && !hello){
        std::memcpy(&handle,bytes.data()+44,2);hello=true;return;
    }
    if(kind==1 && status.phase==Phase::Encrypting && status.store==Store::Empty){
        if(candidate.Valid()){Fail(Invalid());return;} // Never replace one candidate silently.
        std::array<std::uint8_t,36> event{};
        std::copy(binding.peer.begin(),binding.peer.end(),event.begin());event[6]=1;
        std::copy(bytes.begin()+16,bytes.begin()+44,event.begin()+8);
        const bool ok=bond::legacy::Decode(bond::legacy::Layout::AuditedC91Legacy,binding.peer,event,candidate) &&
            rec::SlotValid(candidate.NativeUnion(),1,binding,16,false);
        bond::legacy::Erase(event);if(!ok)Fail(Invalid());return;
    }
    if(kind==4 && status.phase==Phase::Encrypting && !completion){
        std::uint16_t observed{};std::memcpy(&observed,bytes.data()+44,2);
        completion=true;status.native_status=bytes[11];status.encrypted=bytes[46];status.key_bytes=bytes[47];
        if(observed!=handle || !callback){Fail(Invalid());return;}
        callback(binding.peer.data(),2,reference,bytes[11]);
    }
}
void Poll(){
    std::scoped_lock lock(mutex);
    if(!configured || suspended)return;
    if(!loaded){loaded=true;if(!Load()){status.store=Store::Error;Fail(last_result);}return;}
    if(peer_status.phase==cb::PeerPhase::Queued || peer_status.phase==cb::PeerPhase::Waiting){
        if(alive || Now()<peer_status.started_ms || Now()-peer_status.started_ms>=3000 ||
           !PeerWindow(peer_status.capture_deadline_ms)){PeerFail(Invalid());return;}
        if(peer_status.phase==cb::PeerPhase::Queued){peer_status.phase=cb::PeerPhase::Waiting;if(!SendPeer())PeerFail(last_result);}
    }
    if(status.phase!=Phase::Hello && status.phase!=Phase::Encrypting && status.phase!=Phase::Saving)return;
    if(!Owned() || Now()<started || Now()-started>=30000){Encryption().Cancel();Fail(Invalid());return;}
    if(status.phase==Phase::Hello){
#ifdef ARMODULE_WAKE_A3
        if(!hello_sent){hello_sent=true;if(!Send(8))Fail(last_result);return;}
#else
        static bool sent{};if(!sent){sent=true;if(!Send(8))Fail(last_result);return;}
#endif
        if(!hello)return;
        bond::encryption::Target t{binding.peer,started,owned_generation,status.generation?status.generation:1,handle,16,false};
        status.phase=Phase::Encrypting;
        const auto state=Encryption().Begin(t,20000);
        if(state!=bond::encryption::State::Waiting && state!=bond::encryption::State::Complete)Fail(last_result);
        return;
    }
    if(status.phase==Phase::Encrypting){
        const auto state=Encryption().Poll();if(state==bond::encryption::State::Waiting)return;
        if(!Encryption().TakeReady()){Fail(Invalid());return;}
        if(status.store==Store::Saved){
#ifdef ARMODULE_WAKE_A3
            if(!wake.Complete(status.nonce,status.connection,status.owner_epoch)){Fail(Invalid());return;}
#endif
            status.phase=Phase::Ready;previous.Clear();return;}
        if(!candidate.Valid()){Fail(Invalid());return;}
        status.phase=Phase::Saving;
    }
    rec::Materials keys{&candidate,nullptr,nullptr,nullptr,nullptr};
    if(!rec::Build(binding,1,keys,1,16,false,next) || !Provision()){Fail(last_result);return;}
    bond::save_io::DeviceSlot slot;
    const auto receipt=slot.Replace(previous.Read(),next.Read(),[]{return Owned() && Now()-started<30000;});
    candidate.Clear();previous.Clear();next.Clear();
    if(receipt.outcome!=bond::save_io::Outcome::CommittedAndReadBack){status.store=Store::Error;Fail(receipt.result);return;}
    status.store=Store::Saved;status.generation=1;status.phase=Phase::Ready;
}
}
