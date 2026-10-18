#pragma once
#include "../src/PostBattleContinuation.hpp"
#include <cassert>
#include <stdexcept>
inline void test_post_battle_continuation(){
    using C=sc6::research::PostBattleContinuation;
    using R=sc6::research::PostBattleSettlement::Result;
    const C::Identity old{11,12,1,20,1,9},next{11,12,1,21,2,9};
    // Out-of-order final truth, contended retirement, between-round playback,
    // then bilateral startup. Native side effects must happen exactly once.
    C c;assert(c.retain(old,100,true));
    assert(!c.committed(old));
    unsigned settlements{},retirements{},commits{},observations{},arms{};
    auto settle=[&]{return ++settlements<3?R::waiting:R::ready;};
    auto retire=[&]{return ++retirements<2?C::Step::waiting:C::Step::complete;};
    auto commit=[&]{assert(settlements==3 && retirements==2);++commits;return true;};
    auto observe=[&](C::Identity& out){out=next;return ++observations<4?C::Step::waiting:C::Step::complete;};
    auto arm=[&](const C::Identity& id){assert(id==next && commits==1);return ++arms<3?C::Step::waiting:C::Step::complete;};
    for(unsigned i=0;i<20;++i)assert(c.poll(9,100,settle,retire,commit,observe,arm));
    assert(c.phase()==C::Phase::active && commits==1 && settlements==3 && retirements==2 && observations==6 && arms==3);
    assert(c.committed(old) && !c.committed(next));
    assert(c.diagnostics.settlement_waits==2 && c.diagnostics.retirement_waits==1 &&
        c.diagnostics.fight_waits==3 && c.diagnostics.rearm_waits==2 && c.diagnostics.rearms==1);
    // Fail at each boundary: never run a later operation or repeat commit.
    for(unsigned failure=0;failure<6;++failure){
        C f;assert(f.retain(old,200,true));unsigned calls[5]{};
        auto s=[&]{++calls[0];return failure==0?R::restart_required:R::ready;};
        auto r=[&]{++calls[1];return failure==1?C::Step::failed:C::Step::complete;};
        auto k=[&]{++calls[2];if(failure==5)throw std::runtime_error("native commit");return failure!=2;};
        auto o=[&](C::Identity& out){++calls[3];out=next;return failure==3?C::Step::failed:C::Step::complete;};
        auto a=[&](const C::Identity&){++calls[4];return C::Step::failed;};
        assert(!f.poll(9,200,s,r,k,o,a));
        assert(!f.committed(old));
        const auto polls=f.diagnostics.polls;
        assert(!f.poll(9,200,s,r,k,o,a) && f.diagnostics.polls==polls);
        const unsigned last=failure==5?2:failure;
        for(unsigned i=0;i<5;++i)assert(calls[i]==(i<=last?1u:0u));
    }
    // No stale session/peer/thread/event can rearm even if marked fight-ready.
    for(unsigned field=0;field<7;++field){
        C f;assert(f.retain(old,300,true));auto bad=next;
        switch(field){case 0:bad.session++;break;case 1:bad.peer++;break;case 2:bad.owner++;break;
            case 3:bad.event=old.event;break;case 4:bad.generation=0;break;case 5:bad.round=0;break;default:bad.event=old.event-1;}
        bool armed=false;
        assert(!f.poll(9,300,[]{return R::ready;},[]{return C::Step::complete;},[]{return true;},
            [&](C::Identity& out){out=bad;return C::Step::complete;},[&](const C::Identity&){armed=true;return C::Step::complete;}));
        assert(!armed && f.failure()==C::Failure::identity);
    }
    // Reentry during a native callback cannot complete or rearm the outer poll.
    C recursive;assert(recursive.retain(old,400,true));bool armed=false;
    auto s=[]{return R::ready;};auto r=[]{return C::Step::complete;};auto k=[]{return true;};
    auto o=[&](C::Identity& out){out=next;return C::Step::complete;};
    auto a=[&](const C::Identity&){armed=true;return C::Step::complete;};
    assert(!recursive.poll(9,400,s,r,[&]{assert(!recursive.poll(9,400,s,r,k,o,a));return true;},o,a));
    assert(!armed && recursive.failure()==C::Failure::reentry && recursive.diagnostics.commit_calls==1);
    C no_custody;assert(!no_custody.retain(old,1,false));
    {
        C waiting;assert(waiting.retain(old,500,true));unsigned attempts=0;
        auto candidate=next;bool fighting=true;
        auto observe_fight=[&](C::Identity& out){out=candidate;return fighting?C::Step::complete:C::Step::waiting;};
        auto pending_arm=[&](const C::Identity&){++attempts;return C::Step::waiting;};
        assert(waiting.poll(9,500,s,r,k,observe_fight,pending_arm) && attempts==1);
        fighting=false;
        assert(waiting.poll(9,500,s,r,k,observe_fight,pending_arm) && attempts==1);
        fighting=true;++candidate.event;
        assert(!waiting.poll(9,500,s,r,k,observe_fight,pending_arm) && attempts==1);
        assert(waiting.failure()==C::Failure::identity && waiting.diagnostics.commit_calls==1);
    }
    C wrong_owner;assert(wrong_owner.retain(old,1,true));assert(!wrong_owner.poll(10,1,s,r,k,o,a));
    C wrong_token;assert(wrong_token.retain(old,1,true));assert(!wrong_token.poll(9,2,s,r,k,o,a));
}
