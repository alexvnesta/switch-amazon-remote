// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "native_guard_diagnostic.hpp"
#include <array>
namespace arnativebond {
// The real guard algorithm is shared with host tests. Platform provides only
// clock/file/process adapters; Bluetooth calls below use the actual SDK seam.
// System getter validates the module-owned connection without adopting it or
// impersonating its applet identity. All original READY predicates remain;
// saved system/user agreement is required in addition. Diagnostics are saved
// AFTER Attempt returns, never between intent and request revalidation.
template<class Platform> class GuardBackend:public Driver {
    Platform &platform_;bool system_open_{};
public:
    const char *reason="Connection check did not pass.";
    std::array<GuardDiagnostic,2> diagnostics{};unsigned diagnostic_count{};
    explicit GuardBackend(Platform &p):platform_(p){}
    ~GuardBackend(){Close();}
    std::uint64_t Now(){return platform_.Now();}
    bool Supported(){return platform_.Supported();}
    bool OperatorConfirmed(){return true;} // Only constructed at physical A edge.
    void Close(){Driver::Close();if(system_open_){btmExit();system_open_=false;}}
    bool PersistIntent(const Record &r){return Valid(r) && platform_.PersistIntent(r);}
    Result Guard(BtdrvAddress target,Binding &binding){
        binding={};if(diagnostic_count>=diagnostics.size())return Refusal;
        auto &d=diagnostics[diagnostic_count++];d={};d.call=diagnostic_count;
        d.begin_ms=d.check_ms=Now();d.firmware=platform_.Firmware();
        const auto finish=[&](GuardStage stage,Result result){
            d.stage=unsigned(stage);d.result=result;d.end_ms=Now();
            d.checksum=DiagnosticChecksum(d);reason=GuardReason(stage);return result;
        };
        if(!platform_.VerifyBinary())return finish(GuardStage::Binary,Refusal);
        d.flags|=BinaryMatched;
        std::uint64_t pid{};platform_.QueryProcess(d.pm_init,d.pm_query,pid);
        if(d.pm_init)return finish(GuardStage::ProcessInit,d.pm_init);
        if(d.pm_query || !pid)return finish(GuardStage::ProcessQuery,d.pm_query?d.pm_query:Refusal);
        d.flags|=ProcessPresent;
        if(!platform_.StorageReady())return finish(GuardStage::Storage,Refusal);
        if(!platform_.ReadStartup(d.startup))return finish(GuardStage::StartupRead,Refusal);
        d.flags|=StartupRead;
        if(!armodule::ValidStartup(d.startup) || d.startup.stage!=unsigned(armodule::StartupStage::Status) || d.startup.result)
            return finish(GuardStage::StartupInvalid,Refusal);
        if(!platform_.ReadContext(d.context))return finish(GuardStage::ContextRead,Refusal);
        d.flags|=ContextRead;
        d.status_read=platform_.ReadStatus(d.status,Now());
        if(d.status_read)return finish(GuardStage::StatusRead,d.status_read);
        d.flags|=StatusRead;d.check_ms=Now();
        if(!ConnectedReady(d.status,d.startup,d.check_ms))return finish(GuardStage::ConnectedReady,Refusal);
        if(!system_open_){d.connection_open=btmInitialize();if(d.connection_open)return finish(GuardStage::ConnectionOpen,d.connection_open);system_open_=true;}
        else d.connection_open=0;
        d.aruid_zero=platform_.AruidZero();d.flags|=AruidObserved;
        std::array<BtdrvBleConnectionInfo,16> connections{};u8 count{};
        d.connection_getter=btmBleGetConnectionState(connections.data(),connections.size(),&count);
        if(d.connection_getter)return finish(GuardStage::ConnectionGetter,d.connection_getter);
        d.flags|=ConnectionsRead;d.count=count;
        if(count>=connections.size())return finish(GuardStage::ListBounds,Refusal);
        std::array<araudit::Connection,16> normalized{};araudit::Address configured{};
        std::memcpy(configured.data(),target.address,configured.size());
        for(unsigned i=0;i<count;++i){
            std::memcpy(normalized[i].address.data(),connections[i].addr.address,configured.size());
            normalized[i].handle=connections[i].connection_handle;
        }
        araudit::ConnectionQuery query{};query.list.result=0;
        araudit::CountConnections(query,configured,normalized,count);
        d.query_result=query.list.result;d.matches=query.list.matches;
        d.invalid=query.list.invalid;d.live_handle=query.target_handle;
        if(query.list.result || query.list.invalid)return finish(GuardStage::ListInvalid,Refusal);
        if(query.list.matches!=1)return finish(GuardStage::TargetMissing,Refusal);
        d.check_ms=Now();
        if(!SystemReadyBinding(d.status,d.startup,d.context,d.check_ms,d.live_handle))
            return finish(GuardStage::ContextBinding,Refusal);
        binding={d.status.instance,d.live_handle};return finish(GuardStage::Passed,0);
    }
};
}
