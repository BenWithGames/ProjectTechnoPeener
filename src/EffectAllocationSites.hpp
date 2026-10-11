#pragma once
#include <array>
#include <cstdint>
namespace sc6::research {
struct EffectAllocationSite {uintptr_t caller,bytes,vtable;unsigned type;};
inline constexpr std::array<EffectAllocationSite,4> effect_allocation_sites{{
    {0x334A1E,0x1E0,0x3E88CE8,1}, {0x2FC720,0x180,0x3E88D18,2},
    {0x334516,0x130,0x3E88C88,3}, {0x3345B4,0x130,0x3E88CB8,4}
}};
inline const EffectAllocationSite* effect_allocation_site(uintptr_t caller) noexcept {
    for(const auto& site:effect_allocation_sites)if(site.caller==caller)return &site;
    return nullptr;
}
}
