#pragma once
#include "EffectEnvironment.hpp"
#include <vector>
#include <cstring>
#include <span>
namespace sc6::research {
// Extents come from allocator/resource ownership callbacks, never native
// relocation ranges. The caller holds their strong references until retirement.
struct EffectOwnedRange {uintptr_t address{},size{},control{};};
struct EffectExternalSource {
    void* context{};
    bool (*ranges)(void*,std::array<EffectOwnedRange,3>&,unsigned&){};
    bool available()const{return context && ranges;}
};
class EffectExternalReferences {
public:
    struct Diagnostic {unsigned check{},visits{},column{},interval{},candidate{};uintptr_t address{},extent{};} diagnostic;
    static constexpr unsigned traversal_limit=65536;
private:
    bool reject(unsigned check,uintptr_t address=0,uintptr_t extent=0) noexcept {
        diagnostic.check=check;diagnostic.address=address;diagnostic.extent=extent;return false;
    }
    std::array<EffectOwnedRange,3> ranges_{};
    std::array<std::vector<std::byte>,3> bytes_{};
    unsigned count_{};
    EffectExternalSource source_{};
    EffectEnvironment environment_{};
    uintptr_t module_{};
    uintptr_t query_{};std::array<std::byte,0x418> query_bytes_{};
    static bool copy(void* destination,uintptr_t address,size_t size) noexcept {
        __try {std::memcpy(destination,reinterpret_cast<const void*>(address),size);return true;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool equal(const void* saved,uintptr_t address,size_t size) noexcept {
        __try{return std::memcmp(saved,reinterpret_cast<const void*>(address),size)==0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool owns(uintptr_t address,size_t size)const noexcept {
        if(!address || !size || address>UINTPTR_MAX-size)return false;
        for(unsigned i=0;i<count_;++i){const auto& r=ranges_[i];
            if(address>=r.address && address-r.address<=r.size && size<=r.size-(address-r.address))return true;
        }
        return false;
    }
    template<class T> bool read(uintptr_t address,T& value)const noexcept {
        return owns(address,sizeof(T)) && copy(&value,address,sizeof(T));
    }
    bool graph(uintptr_t query) noexcept {
        diagnostic={};
        uintptr_t descriptor{};unsigned char active{};
        if(!copy(&descriptor,query,8) || !copy(&active,query+0x410,1))return reject(1,query,0x411);
        // Native 391356/391366 returns immediately for either condition.
        if(!active || !descriptor)return true;
        int16_t columns{};
        if(!owns(descriptor,0x2A) || !read(descriptor+0x28,columns))return reject(2,descriptor,0x2A);
        if(columns<0 || columns>128)return reject(3,descriptor+0x28,static_cast<uint16_t>(columns));
        for(unsigned i=0;i<static_cast<unsigned>(columns);++i){
            diagnostic.column=i;diagnostic.interval=diagnostic.candidate=0;
            uintptr_t cell{},intervals{};uint16_t interval_count{};
            if(!copy(&cell,query+8+i*8,8))return reject(4,query+8+i*8,8);
            if(!read(cell,intervals) || !read(cell+8,interval_count))return reject(5,cell,10);
            if(interval_count && !owns(intervals,size_t(interval_count)*8))return reject(6,intervals,size_t(interval_count)*8);
            for(unsigned j=0;j<interval_count;++j){
                diagnostic.interval=j;diagnostic.candidate=0;
                if(++diagnostic.visits>traversal_limit)return reject(7,intervals+j*8,8);
                uintptr_t interval{};uint16_t candidates{};
                if(!read(intervals+j*8,interval) || !owns(interval,16) || !read(interval+8,candidates))return reject(8,interval,16);
                if(candidates && !owns(interval+16,size_t(candidates)*8))return reject(9,interval+16,size_t(candidates)*8);
                for(unsigned k=0;k<candidates;++k){
                    diagnostic.candidate=k;
                    if(++diagnostic.visits>traversal_limit)return reject(10,interval+16+k*8,8);
                    uintptr_t candidate{};
                    if(!read(interval+16+k*8,candidate) || !owns(candidate,0x40))return reject(11,candidate,0x40);
                }
            }
        }
        return true;
    }

public:
    enum class Failure:unsigned {none,source,extent,read,environment,graph,changed};
    Failure failure{};bool valid{};
    bool capture(uintptr_t module,EffectExternalSource source){
        valid=false;failure=Failure::source;module_=module;source_=source;count_=0;
        if(!module || module>UINTPTR_MAX-0x4860000 || !source.available() || !source.ranges(source.context,ranges_,count_) || !count_ || count_>3)return false;
        failure=Failure::extent;
        for(unsigned i=0;i<count_;++i){const auto& range=ranges_[i];
            if(!range.address || !range.control || !range.size || range.size>1024*1024 || range.address>UINTPTR_MAX-range.size)return false;
            for(unsigned j=0;j<i;++j)if(range.address<ranges_[j].address+ranges_[j].size && ranges_[j].address<range.address+range.size)return false;
            bytes_[i].resize(range.size);
            failure=Failure::read;if(!copy(bytes_[i].data(),range.address,range.size))return false;
        }
        failure=Failure::environment;
        if(!EffectEnvironment::capture(module,environment_))return false;
        // Known manager storage and scratch allocations must be module-resident.
        // Unknown heap scratch is not granted ownership by VirtualQuery/readability.
        if(environment_.values[2]!=module+0x4100B60)return false;
        for(unsigned i:{6u,7u})if(environment_.values[i]<module || environment_.values[i]>module+0x4860000-16)return false;
        failure=Failure::graph;
        query_=module+(environment_.values[1]?0x4845E80:0x4844DD0);
        if(!copy(query_bytes_.data(),query_,query_bytes_.size()) || !graph(query_))return false;
        valid=true;if(!validate()){valid=false;return false;}failure=Failure::none;return true;
    }
    bool validate(){
        if(!valid || !source_.available())return false;
        std::array<EffectOwnedRange,3> current{};unsigned count{};
        failure=Failure::source;
        if(!source_.ranges(source_.context,current,count) || count!=count_)return false;
        for(unsigned i=0;i<count_;++i){const auto& r=ranges_[i];
            if(current[i].address!=r.address || current[i].size!=r.size || current[i].control!=r.control)return false;
            failure=Failure::changed;if(!equal(bytes_[i].data(),r.address,r.size))return false;
        }
        EffectEnvironment live;failure=Failure::environment;
        if(!EffectEnvironment::capture(module_,live) || !environment_.same(live))return false;
        if(!equal(query_bytes_.data(),query_,query_bytes_.size()))return false;
        failure=Failure::graph;
        if(!graph(module_+(environment_.values[1]?0x4845E80:0x4844DD0)))return false;
        failure=Failure::none;return true;
    }
};
}
