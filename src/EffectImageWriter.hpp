#pragma once
#include "NativeEffectImage.hpp"
namespace sc6::research {
// Low-level writer. Only call under the lifetime gate AFTER fresh preflight and
// validation of external references. Failure may leave partial writes; quarantine
// the runtime and require restart, never collect objects from a partial restore.
template<unsigned Capacity> struct EffectImageWriter {
    static bool write(const NativeEffectImage<Capacity>& saved,unsigned& exception) noexcept {
        exception=0;
        __try {
            for(unsigned i=0;i<saved.count;++i){const auto& object=saved.objects[i];
                std::memcpy(reinterpret_cast<void*>(object.address),object.bytes.data(),object.size);
            }
            *reinterpret_cast<uintptr_t*>(saved.manager)=saved.head;
            for(unsigned i=0;i<saved.count;++i){const auto& object=saved.objects[i];
                if(std::memcmp(reinterpret_cast<void*>(object.address),object.bytes.data(),object.size))return false;
            }
            return *reinterpret_cast<uintptr_t*>(saved.manager)==saved.head;
        }__except(EXCEPTION_EXECUTE_HANDLER){exception=GetExceptionCode();return false;}
    }
};
}
