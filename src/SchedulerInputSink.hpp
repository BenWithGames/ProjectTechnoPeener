#pragma once
#include "NetworkGameHandoff.hpp"
namespace sc6 {
enum class SchedulerPublish {accepted,full,rejected};
// Immutable process-lifetime sink, installed before the associated capture.
// The network worker is the sole producer. No game memory is touched here.
struct SchedulerInputSink {
    void* context{};
    SchedulerPublish (*send)(void*,const NetworkGameHandoff::Request&){};
};
inline std::atomic<const SchedulerInputSink*> scheduler_input_sink{};
inline SchedulerPublish submit_scheduler_input(const SchedulerInputSink& sink,
    const NetworkGameHandoff::Request& input,bool& delivered){
    if(delivered)return SchedulerPublish::accepted;
    if(!sink.context || !sink.send)return SchedulerPublish::rejected;
    const auto result=sink.send(sink.context,input);
    if(result==SchedulerPublish::accepted)delivered=true;
    return result;
}
}
