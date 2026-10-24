#pragma once
#include <Windows.h>
#include "SteamHandshakeProtocol.hpp"
namespace sc6::research {
// Transport evidence only: no claim of private mode, frame alignment or consent
// to mutate gameplay. Readers must separately validate those conditions.
class CombatPeerContext {
public:
    struct Snapshot {uint64_t peer{},local_nonce{},remote_nonce{},tick{};unsigned local_slot{};bool valid{};uint64_t control_tick{},heartbeat_tick{};uint32_t heartbeat_sequence{};};
private:
    SRWLOCK lock_=SRWLOCK_INIT;Snapshot latest_{};
public:
    void clear(){AcquireSRWLockExclusive(&lock_);latest_={};ReleaseSRWLockExclusive(&lock_);}
    bool publish(const steam_handshake::Protocol& protocol,unsigned slot,uint64_t now,bool live){
        Snapshot value;
        if(live && protocol.mutual && protocol.proof && protocol.expected_peer && protocol.local_nonce &&
            protocol.remote_nonce && protocol.local_nonce!=protocol.remote_nonce && slot>=1 && slot<=2)
            value={protocol.expected_peer,protocol.local_nonce,protocol.remote_nonce,now,slot,true};
        AcquireSRWLockExclusive(&lock_);
        if(value.valid){
            value.control_tick=now;
            if(latest_.valid && latest_.peer==value.peer && latest_.local_nonce==value.local_nonce &&
               latest_.remote_nonce==value.remote_nonce && latest_.local_slot==value.local_slot){
                value.heartbeat_tick=latest_.heartbeat_tick;value.heartbeat_sequence=latest_.heartbeat_sequence;
                if(latest_.tick>value.tick)value.tick=latest_.tick;
            }
        }
        latest_=value;ReleaseSRWLockExclusive(&lock_);return value.valid;
    }
    // A received heartbeat may refresh an established identity, never create one.
    // Monotonic sequence rejects duplicates, reordering and old-match traffic.
    bool heartbeat(uint64_t peer,uint64_t local,uint64_t remote,uint32_t sequence,uint64_t now){
        AcquireSRWLockExclusive(&lock_);
        const bool valid=latest_.valid && latest_.peer==peer && latest_.local_nonce==local &&
            latest_.remote_nonce==remote && sequence>latest_.heartbeat_sequence && now>=latest_.tick;
        if(valid){latest_.tick=latest_.heartbeat_tick=now;latest_.heartbeat_sequence=sequence;}
        ReleaseSRWLockExclusive(&lock_);return valid;
    }
    enum class Check : unsigned {ready,missing,future,stale};
    static Check classify(uint64_t now,const Snapshot& value){
        if(!value.valid)return Check::missing;
        if(now<value.tick)return Check::future;
        return now-value.tick>250?Check::stale:Check::ready;
    }
    template<class Clock> Check inspect_current(Snapshot& out,uint64_t& now,Clock clock){
        AcquireSRWLockShared(&lock_);out=latest_;ReleaseSRWLockShared(&lock_);
        // Sample AFTER copying: a publisher cannot make this observation newer
        // than an earlier caller timestamp sampled before lock acquisition.
        now=clock();return classify(now,out);
    }
    Check inspect_current(Snapshot& out,uint64_t& now){return inspect_current(out,now,[]{return GetTickCount64();});}
    bool read(uint64_t now,Snapshot& out){
        // Writers only replace this small value under the lock; no I/O or
        // game calls occur there. Contention must not look like lost consent
        // and silently drop a one-shot capture boundary.
        out={};AcquireSRWLockShared(&lock_);
        const auto value=latest_;ReleaseSRWLockShared(&lock_);
        if(!value.valid || now<value.tick || now-value.tick>250)return false;
        out=value;return true;
    }
};
inline CombatPeerContext combat_peer_context;
}
