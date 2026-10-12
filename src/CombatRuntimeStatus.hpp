#pragma once
#include <atomic>
#include <cstdint>
#include <ostream>
#include <array>
#include "NativeRestoreDiagnostic.hpp"
namespace sc6::research {
// Published once after preparation. Object/context have process lifetime.
// The worker may request a stop but may not release game-owned references.
struct CombatRuntimeStatus {
    enum Reason : unsigned {none,requested,backend_failure,capture_limit,activation_failure,exception,round_boundary};
    uint64_t session{};
    void* context{};
    void (*drain)(void*,std::ostream&,std::ostream&){};
    std::atomic<bool> stop_requested{},active{},restart_required{},history_retired{};
    std::atomic<unsigned> reason{},adapter_failure{},adapter_exception{};
    std::atomic<uint64_t> updates{};
    std::atomic<bool> restored{};
    std::array<std::atomic<uint64_t>,NativeRestoreDiagnostic::count> restore_details{};
    void request_stop(){stop_requested.store(true,std::memory_order_release);}
    void write(std::ostream& events,std::ostream& summary){
        if(drain)drain(context,events,summary);
        summary<<"continuous_runtime_schema=1\ncontinuous_runtime_active="<<active.load()
            <<"\ncontinuous_stop_requested="<<stop_requested.load()
            <<"\ncontinuous_stop_reason="<<reason.load()
            <<"\ncontinuous_restart_required="<<restart_required.load()
            <<"\ncontinuous_history_retired="<<history_retired.load()
            <<"\ncontinuous_adapter_failure="<<adapter_failure.load()
            <<"\ncontinuous_adapter_exception="<<adapter_exception.load()<<'\n';
        summary<<"continuous_native_updates="<<updates.load()<<"\ncontinuous_native_restore_invoked="<<restored.load()<<'\n';
        for(unsigned i=0;i<restore_details.size();++i)summary<<"continuous_restore_"<<NativeRestoreDiagnostic::names[i]<<'='<<restore_details[i].load()<<'\n';
    }
};
inline std::atomic<CombatRuntimeStatus*> combat_runtime_status{};
}
