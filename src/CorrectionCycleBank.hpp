#pragma once
#include "StartupPredictionCorrection.hpp"
#include "NativeHoldRehearsal.hpp"
#include <memory>
namespace sc6::research {
// Construct before arming. Objects never move or get reset while game/network
// threads can access them. The worker selects a cycle; the owner performs work.
template<class Runner=StartupPredictionCorrection,class Hold=NativeHoldRehearsal<>>
class CorrectionCycleBank {
public:
    static constexpr unsigned count=12;
    const bool combat;
    const unsigned limit;
    const unsigned minimum_hold_ms;
    struct Cycle {Runner runner;Hold entry,confirmation;};
    using Words=StartupCorrectionExchange::Words;
private:
    std::array<std::unique_ptr<Cycle>,count> cycles_;
    std::atomic<unsigned> active_{};
public:
    explicit CorrectionCycleBank(bool attack=false,bool combat_mode=false,unsigned hold_ms=0):combat(combat_mode),limit(combat_mode?count:3),minimum_hold_ms(combat_mode?hold_ms:100){for(unsigned i=0;i<limit;++i){auto& c=cycles_[i];c=std::make_unique<Cycle>();if(attack)c->runner.configure_attack();if(combat)c->runner.configure_combat();}}
    bool neutral_skipped(unsigned i)const{const auto& c=at(i);const auto& r=c.runner;
        return combat && r.exchange.input_rejection.load()==2 && r.retired.load() &&
            !r.restart.load() && !r.restored.load() && !r.injected.load() && r.stage.load()==0 &&
            c.confirmation.state.load()==Hold::rejected;
    }
    bool unchanged_skipped(unsigned i)const{const auto& r=at(i).runner;
        return combat && r.no_divergence.load() && r.retired.load() && !r.failed.load() && !r.restart.load();
    }
    bool skipped(unsigned i)const{return neutral_skipped(i) || unchanged_skipped(i);}
    unsigned neutral_count()const{unsigned n=0;for(unsigned i=0;i<active();++i)if(neutral_skipped(i))++n;return n;}
    unsigned unchanged_count()const{unsigned n=0;for(unsigned i=0;i<active();++i)if(unchanged_skipped(i))++n;return n;}
    unsigned skipped_count()const{unsigned n=0;for(unsigned i=0;i<active();++i)if(skipped(i))++n;return n;}
    unsigned active()const{return active_.load(std::memory_order_acquire);}
    Cycle* current(){const auto i=active();return i?cycles_[i-1].get():nullptr;}
    const Cycle& at(unsigned index)const{return *cycles_.at(index);}
    // Worker only. A fresh exchange and both fresh holds are published as one
    // cycle. Caller must additionally have authenticated peer retirement.
    bool arm(unsigned cycle,unsigned identity,unsigned clock,unsigned battle,unsigned slot){
        const auto previous=active();
        if(cycle!=previous+1 || cycle>limit || !clock || !battle || clock>UINT32_MAX-3 || battle>UINT32_MAX-3)return false;
        if(previous){const auto& r=cycles_[previous-1]->runner;
            if(!skipped(previous-1) && (!r.verified.load() || !r.retired.load() || r.failed.load() || r.restart.load()))return false;
        }
        auto& c=*cycles_[cycle-1];
        if(!c.runner.exchange.configure(battle,slot) || !c.entry.request(identity,clock,battle,1000,minimum_hold_ms) ||
           !c.confirmation.request(identity,clock+3,battle+3,2000,minimum_hold_ms))return false;
        active_.store(cycle,std::memory_order_release);return true;
    }
    // Outer protocol authenticates sender and nonces. Encode the cycle in the
    // packet index so delayed data never reaches another cycle's exchange.
    static unsigned packet_index(unsigned cycle,unsigned row){return cycle*4+row;}
    bool receive(bool certificate,unsigned index,const Words& words){
        const auto cycle=index/4,row=index%4;
        if(!cycle || cycle!=active() || cycle>limit || (certificate?row!=3:row>=3))return false;
        auto& e=cycles_[cycle-1]->runner.exchange;
        const bool ok=certificate?e.receive_certificate(words):e.receive(row,words);
        e.update();return ok;
    }
    bool all_verified()const{
        if(combat){for(unsigned i=0;i<active();++i){const auto& r=at(i).runner;
            if(r.verified.load() && r.retired.load() && !r.failed.load() && !r.restart.load())return true;}return false;}
        if(active()!=limit)return false;
        for(unsigned i=0;i<limit;++i){const auto& r=at(i).runner;
            if(!r.verified.load() || !r.retired.load() || r.failed.load() || r.restart.load())return false;}
        return true;
    }
    template<class Stream> void write_evidence(Stream& out,const std::filesystem::path& report)const{
        for(unsigned i=0;i<active();++i){const auto& e=at(i).runner.failure_evidence;
            const auto file=report.parent_path()/(report.stem().string()+"-cycle-"+std::to_string(i+1)+"-evidence.bin");
            out<<"suite_cycle_"<<i+1<<"_evidence_state="<<e.state.load()<<'\n';
            if(e.state.load()==1)out<<"suite_cycle_"<<i+1<<"_evidence_written="<<e.write(file)<<'\n';
        }
    }
    template<class Stream> void write_report(Stream& out)const{
        out<<"suite_cycles_target="<<(combat?1:3)<<"\nsuite_attempt_limit="<<limit<<"\nsuite_neutral_windows_skipped="<<neutral_count()<<"\nsuite_no_divergence_windows_skipped="<<unchanged_count()<<"\nsuite_cycles_active="<<active()<<"\nsuite_cycles_verified="<<all_verified()<<'\n';
        for(unsigned i=0;i<limit;++i){const auto& r=cycles_[i]->runner;
            const auto p="suite_cycle_"+std::to_string(i+1)+"_";
            out<<p<<"captured="<<r.captured.load()<<'\n'<<p<<"predicted="<<r.predicted.load()<<'\n'
               <<p<<"corrected="<<r.corrected.load()<<'\n'<<p<<"verified="<<r.verified.load()<<'\n'
               <<p<<"retired="<<r.retired.load()<<'\n'<<p<<"failed="<<r.failed.load()<<'\n'<<p<<"restart="<<r.restart.load()<<'\n';
            out<<p<<"failure_reason="<<r.failure_reason.load()<<'\n'<<p<<"capture_detail="<<r.capture_detail.load()<<'\n'
               <<p<<"confirmation_local="<<r.exchange.local_count.load()<<'\n'<<p<<"confirmation_remote="<<r.exchange.remote_count.load()<<'\n'
               <<p<<"neutral_skipped="<<neutral_skipped(i)<<'\n'<<p<<"no_divergence_skipped="<<unchanged_skipped(i)<<'\n'<<p<<"input_rejection="<<r.exchange.input_rejection.load()<<'\n'
               <<p<<"confirmation_ready="<<r.exchange.ready.load()<<'\n'<<p<<"confirmation_failed="<<r.exchange.failed.load()<<'\n'
               <<p<<"entry_hold="<<cycles_[i]->entry.state.load()<<'\n'<<p<<"confirmation_hold="<<cycles_[i]->confirmation.state.load()<<'\n'
               <<p<<"confirmation_services="<<cycles_[i]->confirmation.services.load()<<'\n';
            cycles_[i]->entry.write_diagnostics(out,p+"entry_diagnostic_");
            cycles_[i]->confirmation.write_diagnostics(out,p+"confirmation_diagnostic_");
            for(unsigned j=0;j<r.diagnostic.size();++j)out<<p<<OwnedPredictionCorrection::diagnostic_names[j]<<'='<<r.diagnostic[j].load()<<'\n';
            const bool valid=r.blend_evidence_valid.load(std::memory_order_acquire);out<<p<<"blend_evidence_valid="<<valid<<'\n';
            if(valid){constexpr char digits[]="0123456789abcdef";
                out<<p<<"blend_saved_hex=";for(const auto& cell:r.blend_saved){const auto v=cell.load();out<<digits[v>>4]<<digits[v&15];}out<<'\n';
                out<<p<<"blend_next_hex=";for(const auto& cell:r.blend_next){const auto v=cell.load();out<<digits[v>>4]<<digits[v&15];}out<<'\n';
            }
        }
    }
    // Owner thread only. Try every independently owned cycle even after a
    // failure. Preserve runners and their diagnostic copies for reporting.
    bool release()noexcept{
        bool ok=true;
        for(auto& c:cycles_)if(c)try{if(!c->runner.release())ok=false;}catch(...){ok=false;}
        return ok;
    }
};
}
