#pragma once
#include <atomic>
#include <cstdint>
namespace sc6::research {
// In-process acknowledgement from the network reader. Session nonces are never
// reused. This is only local attachment evidence, not peer activation evidence.
class OwnedTransportAttachment {
    std::atomic<uint64_t> nonce_{};
public:
    bool attach(uint64_t nonce){
        if(!nonce)return false;uint64_t empty=0;
        return nonce_.compare_exchange_strong(empty,nonce,std::memory_order_acq_rel);
    }
    bool ready(uint64_t nonce)const{return nonce && nonce_.load(std::memory_order_acquire)==nonce;}
    void detach(uint64_t nonce){if(nonce)nonce_.compare_exchange_strong(nonce,0,std::memory_order_acq_rel);}
};
inline OwnedTransportAttachment owned_transport_attachment;
}
