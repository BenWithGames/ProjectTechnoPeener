#pragma once
#include "NativeClockAccess.hpp"
#include <array>
#include <atomic>
namespace sc6::research {
// Audited 3F2B60/3F6770 ring tags, 3F6600 publication and 3FDFA0 waits.
// Classification is NOT authorization to modify native queues or advance.
struct NativePredictionFrontier {
    struct Entry {uint32_t identity{},frame{},buttons{},valid{};};
    struct Sample {uint32_t identity{},frame{},local_cursor{},all_cursor{},local_mask{},phase{},wait_reason{},wait_count{},published{},lead{},battle{};std::array<Entry,2> entries{};};
    enum Reason:unsigned {confirmed=0,remote_missing=1,local_missing=2,transition=3,budget=4,invalid=5};
    static bool present(const Entry& e,const Sample& s){return e.identity==s.identity && e.frame==s.frame && (e.valid&255u)==1;}
    static Reason classify(const Sample& s){
        if((s.local_mask!=1 && s.local_mask!=2) || s.frame>INT32_MAX || s.identity>INT32_MAX)return invalid;
        if(s.phase!=2 || s.wait_reason==1 || s.wait_reason>=4)return transition;
        const unsigned local=s.local_mask==1?0:1;
        if(!present(s.entries[local],s))return local_missing;
        if(present(s.entries[1-local],s))return confirmed;
        // all_cursor is first not-yet-confirmed input, never changed by prediction.
        if(s.all_cursor>s.frame || s.frame-s.all_cursor>=3)return budget;
        return remote_missing;
    }
    static bool eligible(const Sample& sample,unsigned serviced_wait_reason,bool native_wait,bool fight_ready){
        if(!fight_ready || (native_wait && serviced_wait_reason!=0 && serviced_wait_reason!=2))return false;
        auto observed=sample;observed.wait_reason=serviced_wait_reason;
        const auto reason=classify(observed);return reason==confirmed || reason==remote_missing;
    }
    static bool read(uintptr_t base,uintptr_t object,Sample& out) noexcept {
        out={};NativeClockAccess<>::State s;if(!NativeClockAccess<>::inspect(base,object,s))return false;
        __try{
            out.identity=s.identity;out.frame=s.frame;out.local_cursor=s.local_cursor;out.all_cursor=s.all_cursor;
            out.local_mask=s.local_mask;out.phase=s.phase;out.lead=s.lead;
            out.wait_reason=*reinterpret_cast<const uint8_t*>(object+0x4425);
            out.wait_count=*reinterpret_cast<const uint32_t*>(object+0x3A8);
            out.published=*reinterpret_cast<const uint32_t*>(object+0x3B0);
            out.battle=*reinterpret_cast<const uint32_t*>(base+0x470D0C4);
            for(unsigned side=0;side<2;++side)std::memcpy(&out.entries[side],reinterpret_cast<const void*>(object+0x3C0+side*0x2000+(s.frame&511u)*16),16);
            return *reinterpret_cast<const uint32_t*>(object+0x3A0)==s.identity && *reinterpret_cast<const uint32_t*>(object+0x3A4)==s.frame;
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    struct Observation {
        std::array<std::atomic<uint64_t>,6> reasons{};
        std::atomic<uint64_t> samples{},invalid_reads{},sequence{};
        std::array<std::atomic<uint32_t>,19> last{};
        void observe(uintptr_t base,uintptr_t object){Sample s;if(!read(base,object,s)){++invalid_reads;return;}
            ++reasons[classify(s)];++sequence;
            const uint32_t values[]={s.identity,s.frame,s.local_cursor,s.all_cursor,s.local_mask,s.phase,s.wait_reason,s.wait_count,s.published,s.lead,s.battle,
                s.entries[0].identity,s.entries[0].frame,s.entries[0].buttons,s.entries[0].valid,s.entries[1].identity,s.entries[1].frame,s.entries[1].buttons,s.entries[1].valid};
            for(unsigned i=0;i<last.size();++i)last[i]=values[i];++samples;++sequence;
        }
        template<class Stream> void report(Stream& out)const{
            out<<"prediction_frontier_schema=1\nprediction_frontier_phase=pre_service\nprediction_frontier_classification_is_authorization=0\nprediction_wait_bypass_enabled=0\nprediction_activation_blocker=native_prediction_admission_and_round_rearm\n";
            out<<"prediction_frontier_samples="<<samples.load()<<"\nprediction_frontier_invalid_reads="<<invalid_reads.load()<<'\n';
            for(unsigned i=0;i<reasons.size();++i)out<<"prediction_frontier_reason_"<<i<<'='<<reasons[i].load()<<'\n';
            std::array<uint32_t,19> values{};bool coherent=false;
            for(unsigned attempt=0;attempt<3 && !coherent;++attempt){
                const auto before=sequence.load();if(before&1)continue;
                for(unsigned i=0;i<last.size();++i)values[i]=last[i].load();
                coherent=before==sequence.load() && before!=0;
            }
            out<<"prediction_frontier_last_coherent="<<coherent<<'\n';
            if(coherent)for(unsigned i=0;i<values.size();++i)out<<"prediction_frontier_last_"<<i<<'='<<values[i]<<'\n';
        }
    };
};
}
