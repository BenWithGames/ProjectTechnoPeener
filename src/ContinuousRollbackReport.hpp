#pragma once
#include "RollbackJournal.hpp"
#include <algorithm>
namespace sc6::research {
// Logging-worker owned. Consume the SPSC journal; never read the live
// controller's non-atomic counters or write files from the simulation hook.
class ContinuousRollbackReport {
    uint64_t events_{},advanced_{},predicted_{},corrections_{},replayed_{},waits_{},failures_{};
    uint64_t failure_frame_{},dropped_{};
    unsigned depth_{},failure_reason_{};
    bool correction_pending_{};
public:
    template<unsigned Capacity> void drain(RollbackJournal<Capacity>& journal,std::ostream& csv){
        RollbackEvent e;
        while(journal.pop(e)){
            RollbackJournal<Capacity>::row(csv,e);++events_;
            switch(e.kind){
            case RollbackEvent::advance:++advanced_;predicted_+=e.detail!=0;break;
            case RollbackEvent::wait:++waits_;break;
            case RollbackEvent::correction_begin:correction_pending_=true;depth_=(std::max)(depth_,e.detail);break;
            case RollbackEvent::correction_end:correction_pending_=false;++corrections_;replayed_+=e.detail;break;
            case RollbackEvent::failure:++failures_;failure_frame_=e.frame;failure_reason_=e.detail;break;
            default:break;
            }
        }
        dropped_=journal.dropped();
    }
    void write(std::ostream& out)const{
        out<<"continuous_report_schema=1\ncontinuous_events="<<events_
           <<"\ncontinuous_live_frames="<<advanced_<<"\ncontinuous_predicted_frames="<<predicted_
           <<"\ncontinuous_corrections_completed="<<corrections_<<"\ncontinuous_replayed_frames="<<replayed_
           <<"\ncontinuous_max_depth="<<depth_<<"\ncontinuous_wait_events="<<waits_
           <<"\ncontinuous_failures="<<failures_<<"\ncontinuous_failure_frame="<<failure_frame_
           <<"\ncontinuous_failure_reason="<<failure_reason_<<"\ncontinuous_correction_pending="<<correction_pending_
           <<"\ncontinuous_events_dropped="<<dropped_<<'\n';
        // No PASS inference: transport, correction counts, visual outcome and
        // final state agreement are separate evidence.
    }
};
}
