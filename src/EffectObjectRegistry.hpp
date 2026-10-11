#pragma once
#include "EffectRetentionWindow.hpp"
#include "EffectAllocationSites.hpp"
namespace sc6::research {
// Owner-thread helper. Allocation return is pending until the caller validates
// actual manager-list membership and reads the fully constructed object's type.
// No native publication; the observer's allocation event alone cannot pin memory.
template<std::size_t Capacity=128,unsigned Slots=16> class EffectObjectRegistry {
public:
    using Window=EffectRetentionWindow<Capacity,Slots>;
    using Token=typename Window::Token;
private:
    struct Entry {uintptr_t address{},vtable{};std::size_t bytes{};Token token{};};
    std::array<Entry,Capacity> entries_{};
public:
    bool allocation(uintptr_t address,std::size_t bytes,uintptr_t caller_rva,uintptr_t module) noexcept {
        const auto* site=effect_allocation_site(caller_rva);
        const uintptr_t type=site && site->bytes==bytes?site->vtable:0;
        if(!type || !address || address>UINTPTR_MAX-bytes || module>UINTPTR_MAX-type)return false;
        Entry* empty=nullptr;
        for(auto& e:entries_){
            if(e.address && address<e.address+e.bytes && e.address<address+bytes)return false;
            if(!e.address && !empty)empty=&e;
        }
        if(!empty)return false;
        *empty={address,module+type,bytes,{}};return true;
    }
    Token constructed(uintptr_t address,uintptr_t actual_vtable,bool verified_list_member,Window& window) noexcept {
        if(!verified_list_member)return {};
        for(auto& e:entries_)if(e.address==address && e.vtable==actual_vtable){
            if(!e.token.generation)e.token=window.track({e.address,e.vtable,e.bytes});
            return window.live(e.token)?e.token:Token{};
        }
        return {};
    }
    Token lookup(uintptr_t address)const noexcept {
        for(const auto& e:entries_)if(e.address==address)return e.token;
        return {};
    }
    // Owner-thread only, before native free; pending objects have no snapshots.
    bool unexpected_release(uintptr_t address,Window& window) noexcept {
        for(auto& e:entries_)if(e.address==address){
            if(e.token.generation && !window.unexpected_release(e.token))return false;
            e={};return true;
        }
        return false;
    }
    // Call only after actual allocator release, not when an object is just unlinked.
    bool released(uintptr_t address) noexcept {
        for(auto& e:entries_)if(e.address==address){e={};return true;}
        return false;
    }
};
}
