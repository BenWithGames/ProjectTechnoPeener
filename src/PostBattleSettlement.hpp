#pragma once
#include <cstdint>
namespace sc6::research {
// Owner-thread pre-commit barrier. The native caller must retain control of
// the entire outer continuation while waiting, not just skip its delegate call.
// No hook is installed or native callback invoked by this class.
class PostBattleSettlement {
public:
    enum class Result {waiting,ready,restart_required};
    enum class Failure {none,identity,boundary,confirmation,endpoint,reconcile,exception,timeout};
private:
    uint64_t session_{},round_{},head_{};
    unsigned owner_{},frame_{};
    bool entered_{},waiting_{},bound_{},failed_{};
    Failure failure_{};
    Result fail(Failure why){failed_=true;failure_=why;return Result::restart_required;}
public:
    Failure failure()const{return failure_;}
    // Stay on the original native call stack: no fiber/FLS switch and no return
    // to the outer caller while confirmation is merely pending. A failed result
    // is terminal and MUST NOT be interpreted by the native adapter as permission
    // to run callbacks. Only the transport worker may progress during pause().
    template<class Poll,class Clock,class Pause>
    Result wait(Poll poll,Clock clock,Pause pause,uint64_t timeout){
        if(failed_)return Result::restart_required;
        if(waiting_)return fail(Failure::boundary);
        if(!timeout || timeout>10000)return fail(Failure::timeout);
        waiting_=true;
        struct ExitWait {bool& flag;~ExitWait(){flag=false;}} exit_wait{waiting_};
        try {
            const auto start=clock();auto prior=start;
            for(;;){
                const auto now=clock();
                if(now<prior || now-start>=timeout)return fail(Failure::timeout);
                prior=now;
                const auto result=poll();
                if(failed_)return Result::restart_required;
                switch(result){
                    case Result::restart_required:return fail(Failure::reconcile);
                    case Result::ready:return result;
                    case Result::waiting:break;
                }
                pause();
            }
        }catch(...){return fail(Failure::exception);}
    }
    template<class Session,class Adapter,class Drain,class Input>
    Result poll(Session& session,Adapter& adapter,Drain drain,const Input& endpoint,
        uint64_t session_id,uint64_t round,unsigned owner,unsigned frame,bool quiescent){
        if(failed_)return Result::restart_required;
        if(entered_)return fail(Failure::boundary);
        entered_=true;
        struct Exit {bool& flag;~Exit(){flag=false;}} exit{entered_};
        try {
            if(!session_id || !round || !owner)return fail(Failure::identity);
            if(!quiescent || adapter.expected_frame()!=frame)return fail(Failure::boundary);
            if(!bound_){
                if(!session.request_handoff(session_id,round))return fail(Failure::identity);
                session_=session_id;round_=round;owner_=owner;frame_=frame;head_=session.head();bound_=true;
            }
            if(session_id!=session_ || round!=round_ || owner!=owner_ || frame!=frame_ || session.head()!=head_)
                return fail(Failure::identity);
            if(!drain())return fail(Failure::confirmation);
            if(failed_)return Result::restart_required;
            // NativeSchedulerStorage's previous boundary is pre-update. Load
            // needs a current endpoint before reconciling the final frame.
            if(!adapter.begin_boundary(endpoint))return fail(Failure::endpoint);
            if(failed_)return Result::restart_required;
            const bool settled=session.settle_handoff(adapter);
            if(failed_ || adapter.failed() || session.state==Session::State::restart_required ||
                adapter.expected_frame()!=frame_ || session.head()!=head_)return fail(Failure::reconcile);
            if(settled && session.handoff_ready())return Result::ready;
            return Result::waiting;
        }catch(...){return fail(Failure::exception);}
    }
};
}
