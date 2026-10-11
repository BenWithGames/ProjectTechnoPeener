#pragma once
#include <Windows.h>
#include "SteamHandshakeProtocol.hpp"
namespace sc6::research {
// Transport evidence only: no claim of private mode, frame alignment or consent
// to mutate gameplay. Readers must separately validate those conditions.
class CombatPeerContext {
public:
    struct Snapshot {uint64_t peer{},local_nonce{},remote_nonce{},tick{};unsigned local_slot{};bool valid{};};
private:
    SRWLOCK lock_=SRWLOCK_INIT;Snapshot latest_{};
public:
    void clear(){AcquireSRWLockExclusive(&lock_);latest_={};ReleaseSRWLockExclusive(&lock_);}
    bool publish(const steam_handshake::Protocol& protocol,unsigned slot,uint64_t now,bool live){
        Snapshot value;
        if(live && protocol.mutual && protocol.proof && protocol.expected_peer && protocol.local_nonce &&
            protocol.remote_nonce && protocol.local_nonce!=protocol.remote_nonce && slot>=1 && slot<=2)
            value={protocol.expected_peer,protocol.local_nonce,protocol.remote_nonce,now,slot,true};
        AcquireSRWLockExclusive(&lock_);latest_=value;ReleaseSRWLockExclusive(&lock_);return value.valid;
    }
    bool read(uint64_t now,Snapshot& out){
        out={};if(!TryAcquireSRWLockShared(&lock_))return false;
        const auto value=latest_;ReleaseSRWLockShared(&lock_);
        if(!value.valid || now<value.tick || now-value.tick>250)return false;
        out=value;return true;
    }
};
inline CombatPeerContext combat_peer_context;
}
