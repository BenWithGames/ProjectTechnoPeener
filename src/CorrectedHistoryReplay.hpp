#pragma once
#include "NativeHistoryRestore.hpp"
#include "NetworkInputComposer.hpp"
namespace sc6::research {
// One correction transaction. Backend owns capture resources and must invoke
// the original update through SingleUpdateCall, never through the hooked entry.
// Success means completion at the old head, not equality to the predicted branch.
class CorrectedHistoryReplay {
public:
    struct Frame {
        RollbackBoundaryKey key;
        NetworkInputComposer::Envelope local{},peer{};
        bool confirmed{};
    };
    enum class Stage {unused,validation,restore,retire,capture,update,endpoint,complete};
    Stage stage{Stage::unused};
    unsigned completed{};
    bool attempted{},restart_required{};
    template<class History,class Restore,class Backend>
    bool run(History& history,Restore& restore,Backend& backend,
        const std::array<Frame,3>& frames,unsigned depth,const RollbackBoundaryKey& live,uintptr_t base){
        if(attempted)return false;
        attempted=true;stage=Stage::validation;
        if(depth<1 || depth>3 || !base || live.thread!=GetCurrentThreadId())return false;
        std::array<std::array<std::byte,40>,3> inputs{};
        for(unsigned i=0;i<depth;++i){const auto& f=frames[i];
            if(!f.confirmed || !f.key.same_round(live) || f.key.frame>=live.frame ||
                live.frame-f.key.frame!=depth-i)return false;
            const auto composed=NetworkInputComposer::compose(f.local,f.peer,3-live.slot,f.key.frame,f.key.frame);
            if(!composed)return false;
            std::memcpy(inputs[i].data(),composed->data(),inputs[i].size());
            if(!RecordedInput::valid_envelope(inputs[i]))return false;
        }
        // Any failure from here may leave native state or history partially
        // changed. Only full completion permits the caller to resume simulation.
        try {
            if(!backend.preflight(frames[0].key,live))return false;
            restart_required=true;
            stage=Stage::restore;
            if(!restore.run(history,frames[0].key,live,base))return false;
            stage=Stage::retire;
            if(!history.rewind_for_replay(frames[0].key,[](auto&){return true;}))return false;
            for(unsigned i=0;i<depth;++i){
                stage=Stage::capture;
                if(!backend.capture(history,frames[i].key,inputs[i]))return false;
                stage=Stage::update;
                if(!backend.update(frames[i].key,inputs[i]))return false;
                ++completed;
            }
            stage=Stage::endpoint;
            if(!backend.capture_endpoint(history,live))return false;
            stage=Stage::complete;restart_required=false;return true;
        }catch(...){return false;}
    }
};
}
