#pragma once
#include <cstdint>
namespace sc6::research {
// Policy for a future exclusive SC6 update hook. Not used by EntryBridge:
// returning from one battle update does not prove the outer game clock stalls.
// The hook must implement each disposition explicitly before enabling this.
template<class Session> class SimulationBoundaryDriver {
    Session& session_;
    uint64_t session_id_{},round_id_{};
    unsigned owner_{};
    bool active_{},entered_{},failed_{};
public:
    enum class Disposition {pass_through,consumed,waiting,restart_required};
    explicit SimulationBoundaryDriver(Session& session):session_(session){}
    // Only the owner may arm at a verified round start, after both peers opt in
    // and native capture/restore plus coordinated clock control are available.
    bool arm(uint64_t session,uint64_t round,unsigned owner,bool both_opted_in,
             bool private_match,bool native_ready,bool clock_owned){
        if(active_ || failed_ || !owner || !both_opted_in || !private_match || !native_ready || !clock_owned)return false;
        if(!session_.begin(session,round))return false;
        session_id_=session;round_id_=round;owner_=owner;active_=true;return true;
    }
    template<class Adapter,class Input> Disposition tick(uint64_t session,uint64_t round,unsigned thread,
        Adapter& adapter,const Input& local){
        if(failed_)return Disposition::restart_required;
        if(!active_)return Disposition::pass_through;
        if(entered_ || session!=session_id_ || round!=round_id_ || thread!=owner_){failed_=true;return Disposition::restart_required;}
        entered_=true;
        struct Exit {bool& entered;~Exit(){entered=false;}} exit{entered_};
        bool advanced=false;
        try {advanced=session_.advance(adapter,local);}
        catch(...){failed_=true;return Disposition::restart_required;}
        if(failed_){return Disposition::restart_required;}
        if(advanced)return Disposition::consumed;
        if(session_.state==Session::State::waiting)return Disposition::waiting;
        failed_=true;return Disposition::restart_required;
    }
    // No automatic fallback after prediction or mutation. A separately
    // verified clean match restart is required to replace this driver.
    bool active()const{return active_;}
    bool restart_required()const{return failed_;}
};
}
