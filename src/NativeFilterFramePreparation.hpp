#pragma once
#include "RawPredictionRoute.hpp"
#include <Windows.h>
namespace sc6::research {
// Input formation is sequential, but each raw word is needed before the next
// player's filter is known. Build a pending frame, resolve each slot once, then
// seal only after comparing both native held/pressed pairs at battle entry.
template<class Access> class NativeFilterFramePreparation {
public:
    struct Config {uintptr_t outer{},clock{},base{};uint32_t owner{},identity{},clock_origin{},battle_origin{},round{},event{};uint64_t generation{};};
    struct Sample {uintptr_t previous_address{};uint32_t local_raw{},previous{},disabled{};};
    enum class Result {pass,prepared,failed};
private:
    RawPredictionRoute<>& route_;
    Config config_{};
    bool bound_{},failed_{},prediction_enabled_{true};
    unsigned local_slot_{};
    bool fail(){failed_=true;return false;}
public:
    uint64_t filters{},sealed{},failures{};
    explicit NativeFilterFramePreparation(RawPredictionRoute<>& route):route_(route){}
    bool bind(Config config,unsigned local_slot,bool prediction_enabled=true){
        if(bound_ || failed_ || !config.outer || !config.clock || config.owner!=GetCurrentThreadId() ||
           local_slot<1 || local_slot>2 || route_.failed() || route_.delivery().head())return false;
        config_=config;local_slot_=local_slot;prediction_enabled_=prediction_enabled;bound_=true;return true;
    }
    Result filter(uintptr_t outer,unsigned slot,uint32_t reverse,bool suppress_edges,bool suppress_held,
                  bool restrict_buttons,uint32_t& raw){
        if(!bound_ || outer!=config_.outer)return Result::pass;
        auto reject=[&]{fail();++failures;return Result::failed;};
        if(failed_ || GetCurrentThreadId()!=config_.owner || slot>=2)return reject();
        Sample sample;uint32_t native_frame{};
        if(!Access::sample(config_,local_slot_,slot,reverse,sample,native_frame) || native_frame<config_.clock_origin)return reject();
        const uint64_t index=native_frame-config_.clock_origin;
        if(index>UINT32_MAX-config_.battle_origin)return reject();
        const auto head=route_.delivery().head();
        if(index==head){
            std::optional<uint32_t> confirmed;
            if(!prediction_enabled_){
                uint32_t value{};
                if constexpr(requires{Access::confirmed_remote(config_,local_slot_,native_frame,value);}){
                    if(!Access::confirmed_remote(config_,local_slot_,native_frame,value))return reject();
                    confirmed=value;
                }else return reject();
            }
            if(!route_.append(config_.owner,sample.local_raw,{},{},true,confirmed))return reject();
        }else if(head==0 || index!=head-1)return reject();
        if(!route_.resolve_filter(config_.owner,index,slot,{sample.disabled,suppress_edges,suppress_held,restrict_buttons}))return reject();
        const auto* frame=route_.delivery().frame(index);std::array<uint32_t,2> previous{};
        if(!frame || !route_.delivery().previous_raw(index,previous))return reject();
        // A corrected previous raw word must also drive native edge formation.
        // Access rechecks the live pointer/value immediately before this write.
        if(!Access::previous(config_,slot,sample,previous[slot]))return reject();
        raw=frame->raw[slot];++filters;return Result::prepared;
    }
    bool complete(uint32_t battle_frame,const std::array<uint32_t,10>& native){
        if(!bound_ || failed_ || GetCurrentThreadId()!=config_.owner || battle_frame<config_.battle_origin ||
           !Access::boundary(config_,battle_frame) ||
           !route_.complete_frame(config_.owner,battle_frame-config_.battle_origin,native)){++failures;return fail();}
        ++sealed;return true;
    }
    bool failed()const{return failed_ || route_.failed();}
};
struct NativeFilterPreparationRoute {
    void* context{};
    bool (*filter)(void*,uintptr_t,unsigned,uint32_t,bool,bool,bool,uint32_t&){};
    bool (*complete)(void*,uint32_t,const std::array<uint32_t,10>&){};
};
inline std::atomic<const NativeFilterPreparationRoute*> native_filter_preparation_route{};
inline std::atomic<uintptr_t> native_filter_hook_base{};
}
