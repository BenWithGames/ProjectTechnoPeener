#pragma once
#include "CombatPeerContext.hpp"
namespace sc6::research {
struct RearmPeerAdmission {
    enum class Result {ready,waiting,failed};
    unsigned reason{},read_check{};uint64_t since{},waits{};bool waiting{};
    Result check(const CombatPeerContext::Snapshot& s,CombatPeerContext::Check read,uint64_t now,
                 uint64_t peer,uint64_t local,uint64_t remote,unsigned slot,uint64_t shared){
        read_check=static_cast<unsigned>(read);reason=0;
        // Even stale evidence must not conceal a changed identity.
        if(s.valid){
            if(s.peer!=peer)reason=4;
            else if(s.local_nonce!=local)reason=5;
            else if(s.remote_nonce!=remote)reason=6;
            else if(s.local_slot!=slot)reason=7;
        }
        if(shared!=local)reason=8;
        if(reason)return Result::failed;
        if(read==CombatPeerContext::Check::ready){waiting=false;since=0;return Result::ready;}
        reason=read_check;
        if(!waiting){since=now;waiting=true;}
        if(now<since || now-since>=9000){reason=9;return Result::failed;}
        ++waits;return Result::waiting;
    }
};
}
