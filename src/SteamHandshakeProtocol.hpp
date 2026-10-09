#pragma once
#include <cstdint>
namespace sc6::steam_handshake {
// Fixed-size little-endian Windows x64 diagnostic protocol; no game inputs.
inline constexpr uint64_t signature=0x000153433648534bULL;
inline constexpr int channel=27861;
struct Packet {uint64_t magic=signature,nonce=0,echo=0,confirmed=0;};
static_assert(sizeof(Packet)==32);
struct Protocol {
    uint64_t expected_peer=0,local_nonce=0,remote_nonce=0;
    bool proof=false,mutual=false;
    uint64_t expected_signature=signature;
    Packet outgoing() const {return {expected_signature,local_nonce,remote_nonce,proof?1ULL:0ULL};}
    bool receive(uint64_t sender,const Packet& p,unsigned size) {
        if(size!=sizeof(Packet)||sender!=expected_peer||p.magic!=expected_signature||!p.nonce||p.nonce==local_nonce||p.confirmed>1) return false;
        if(p.echo && p.echo!=local_nonce)return false;
        if(p.confirmed && p.echo!=local_nonce)return false;
        if(remote_nonce && remote_nonce!=p.nonce)return false;
        remote_nonce=p.nonce;
        if(p.echo==local_nonce){proof=true;if(p.confirmed)mutual=true;}
        return true;
    }
};
}
