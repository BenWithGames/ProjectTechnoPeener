#pragma once
#include "NativeEffectImage.hpp"
namespace sc6::research {
// Read-only plan. No memory writes or permission to execute a native restore.
template<unsigned Capacity=64> struct EffectRestorePlan {
    std::array<unsigned,Capacity> revive{},remove{};
    unsigned revive_count{},remove_count{},preserve_count{};bool valid{};
    static bool shape(const NativeEffectImage<Capacity>& image,uintptr_t module) noexcept {
        if(!image.valid || !image.manager || image.count>Capacity || !module || module>UINTPTR_MAX-0x3E88D18)return false;
        auto expected=image.head;uintptr_t previous=0;
        for(unsigned i=0;i<image.count;++i){const auto& object=image.objects[i];
            if(!object.address || object.address!=expected || !object.generation || object.size>object.bytes.size() || object.address>UINTPTR_MAX-object.size)return false;
            bool known=false;for(const auto& site:effect_allocation_sites)if(object.vtable==module+site.vtable && object.size==site.bytes)known=true;
            if(!known || NativeEffectImage<Capacity>::pointer(object.bytes.data())!=object.vtable ||
               NativeEffectImage<Capacity>::pointer(object.bytes.data()+0x18)!=previous ||
               NativeEffectImage<Capacity>::pointer(object.bytes.data()+0x28)!=image.manager)return false;
            for(unsigned j=0;j<i;++j){const auto& prior=image.objects[j];
                if(object.address<prior.address+prior.size && prior.address<object.address+object.size)return false;
            }
            previous=object.address;expected=NativeEffectImage<Capacity>::pointer(object.bytes.data()+0x10);
        }
        return !expected;
    }
    template<class Retained> bool prepare(const NativeEffectImage<Capacity>& saved,const NativeEffectImage<Capacity>& current,uintptr_t module,Retained&& retained) {
        valid=false;revive_count=remove_count=preserve_count=0;
        if(saved.manager!=current.manager || !shape(saved,module) || !shape(current,module))return false;
        for(unsigned i=0;i<saved.count;++i){const auto& old=saved.objects[i];
            if(!retained(old.address,old.generation))return false;
            bool found=false;
            for(unsigned j=0;j<current.count;++j){const auto& now=current.objects[j];
                if(old.address==now.address){
                    if(old.generation!=now.generation || old.vtable!=now.vtable || old.size!=now.size)return false;
                    found=true;
                }else if(old.address<now.address+now.size && now.address<old.address+old.size)return false;
            }
            if(found)++preserve_count;else revive[revive_count++]=i;
        }
        for(unsigned j=0;j<current.count;++j){bool found=false;
            for(unsigned i=0;i<saved.count;++i)if(current.objects[j].address==saved.objects[i].address)found=true;
            if(!found)remove[remove_count++]=j;
        }
        valid=true;return true;
    }
};
}
