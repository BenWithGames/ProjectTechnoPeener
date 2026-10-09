#pragma once
#include "NativePredictionProbe.hpp"
namespace sc6::research {
// Bounded two/three natural predicted updates and independently compared neutral replay.
struct NativeTwoMovement {
    using B=NativeBoundarySnapshot;using P=FullStateProbe;using Input=NativePredictionProbe::Input;
    std::array<std::unique_ptr<B>,7> frames;
    std::array<std::unique_ptr<NativeAdjacentRestore>,6> restores;
    NativePredictionProbe readiness{true,false,true};
    std::unique_ptr<NativePredictionProbe::History> history;
    RecordedInput delivery,guard;
    Input actual{},predicted{};
    unsigned window{2},direction_bits{0x400};
    unsigned captured{},restored{},delivered{},compared{};
    uintptr_t next_argument{};bool done{},fault{},corrected{},state_changed{};
    const char* failure_stage="none";
    explicit NativeTwoMovement(unsigned length=2,unsigned bits=0x400):window(length),direction_bits(bits){
        if(window<2 || window>3 || (bits!=0x400 && bits!=0x800)){fault=true;return;}
        for(unsigned i=0;i<2*window+1;++i)frames[i]=std::make_unique<B>(i<=window?B::Mode::idle:B::Mode::direction_observation);
        for(unsigned i=0;i<2*window+1;++i)frames[i]->direction_bits=direction_bits;
        for(unsigned i=0;i<2*window;++i)restores[i]=std::make_unique<NativeAdjacentRestore>();
    }
    unsigned movement_source(unsigned hop)const{return hop?window+hop:0;}
    static bool clean_blend_pair(const P& a,const P& b){
        const auto& x=a.blends[0];const auto& y=b.blends[0];
        if(!x.valid || !y.valid)return false;
        for(unsigned side=0;side<2;++side)
            if(x.owners[side]!=y.owners[side] || std::memcmp(x.bytes[side].data(),y.bytes[side].data(),1372))return false;
        return true;
    }
    static bool clean_camera_pair(const CameraSnapshot& a,const CameraSnapshot& b){
        if(!a.valid || !b.valid || a.count!=b.count || a.count>a.records.size())return false;
        unsigned selected=0;
        for(unsigned i=0;i<a.count;++i){const auto& x=a.records[i];const auto& y=b.records[i];
            if(x.owner!=y.owner || x.smoothing_valid!=y.smoothing_valid)return false;
            if(!x.smoothing_valid)continue;
            ++selected;
            if(x.targets!=y.targets || x.response!=y.response ||
               CameraSnapshot::word(x.targets.data()+104) || CameraSnapshot::word(x.targets.data()+108))return false;
        }
        return selected==1;
    }
    static void reference(NativeAdjacentRestore& r,const B& expected){
        auto& q=*r.probe;const auto& b=*expected.data;
        q.after_save=b.saved;q.after_keys=b.before_keys;
        std::memcpy(q.after_players,b.before_players,sizeof(q.after_players));
        q.original_next=b.measurements[0];q.rings[2]=b.rings[0];
        q.blends[1]=b.blends[0];q.transforms[1]=b.transforms[0];q.cameras[1]=b.cameras[0];
        r.replay_delivered=true;
    }
    __declspec(noinline) bool step(uintptr_t base,uintptr_t argument,bool bridge,
        P::Routine writer=nullptr,P::Routine reader=nullptr,P::Preflight check=nullptr) noexcept {
        next_argument=0;if(fault)return false;if(done)return true;if(!bridge)return fail("bridge");
        if(!captured && !readiness.settle.ready){
            if(!readiness.settle.step(base))return fail("idle_settle");
            if(!readiness.settle.ready)return true;
        }
        if(readiness.waiting_camera){
            if(!readiness.wait_camera())return fail("camera_settle");
            if(readiness.camera_stable<10)return true;
            readiness.waiting_camera=0;frames[0]=std::make_unique<B>(B::Mode::idle);
        }
        if(captured<2*window+1){
            frames[captured]->direction_bits=direction_bits;
            if(!frames[captured]->capture(base,argument,writer,check))return fail("capture");
            if(captured<window+1){
                const auto& camera=frames[captured]->data->cameras[0];
                for(unsigned i=0;i<camera.count;++i){const auto& c=camera.records[i];
                    if(c.smoothing_valid && (!readiness.camera_settled(c) ||
                       (captured && !clean_camera_pair(frames[captured-1]->data->cameras[0],camera)))){
                        readiness.waiting_camera=c.owner;readiness.camera_stable=0;captured=0;
                        for(unsigned j=1;j<=window;++j)frames[j]=std::make_unique<B>(B::Mode::idle);
                        return true;
                    }
                }
            }
            if(captured>0 && captured<window+1 && !clean_blend_pair(*frames[captured-1]->data,*frames[captured]->data)){
                // No load has occurred: restart discovery instead of relaxing preservation.
                captured=0;readiness.settle=IdleBlendSettle{};
                for(unsigned i=0;i<=window;++i)frames[i]=std::make_unique<B>(B::Mode::idle);
                return true;
            }
            if(!captured){
                actual=frames[0]->data->delayed_inputs;
                if(!NativePredictionProbe::make_prediction(actual,predicted,true,direction_bits))return fail("prediction");
            }
            ++captured;
            if(captured<window+1)return true;
            if(captured==window+1){
                for(unsigned i=0;i<window;++i)
                    if(!restores[i]->prepare_captured(*frames[i],*frames[i+1]))return fail("clean_prepare");
                for(unsigned i=window;i-- >0;){
                    if(!restores[i]->restore_captured(*frames[i],base,guard.assign(frames[i+1]->data->delayed_inputs),writer,reader,check))return fail("clean_restore");
                    ++restored;
                }
                history=std::make_unique<NativePredictionProbe::History>(predicted);
            }
            if(captured<2*window+1){Input used{};
                if(!history->advance(used) || used!=predicted)return fail("prediction_delivery");
                next_argument=delivery.assign(used);++delivered;return true;
            }
            const auto& clean=*frames[window]->data;const auto& wrong=*frames[2*window]->data;
            state_changed=!TwoFrameIdleReplay::transforms_equal(clean.transforms[0],wrong.transforms[0]) ||
                !TwoFrameIdleReplay::cameras_equal(clean.cameras[0],wrong.cameras[0]) || clean.measurements[0].fields!=wrong.measurements[0].fields;
            if(!state_changed)return fail("no_divergence");
            for(unsigned i=0;i<window;++i)
                if(history->receive(i,actual)!=NativePredictionProbe::History::Arrival::accepted)return fail("late_inputs");
            if(history->correction()!=std::optional<std::size_t>{0})return fail("late_inputs");
            for(unsigned i=0;i<window;++i)
                if(!restores[window+i]->prepare_captured(*frames[movement_source(i)],*frames[window+i+1],true))return fail("movement_prepare");
            for(unsigned i=window;i-- >0;){
                if(!restores[window+i]->restore_captured(*frames[movement_source(i)],base,guard.assign(frames[window+i+1]->data->delayed_inputs),writer,reader,check))return fail("movement_restore");
                ++restored;
            }
            for(unsigned i=0;i<window;++i)reference(*restores[window+i],*frames[i+1]);
            next_argument=delivery.assign(actual);++delivered;return true;
        }
        if(!restores[window+compared]->replay_step(*frames[compared],base,argument,bridge,writer,reader,check))return fail("neutral_replay");
        ++compared;
        if(compared==window){if(!history->finish_correction(0))return fail("history_finish");corrected=done=true;return true;}
        next_argument=delivery.assign(actual);++delivered;return true;
    }
    bool fail(const char* stage){failure_stage=stage;fault=true;return false;}
};
}
