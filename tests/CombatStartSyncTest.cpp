#include "../src/CombatStartPacket.hpp"
#include <cassert>
#include "../src/RetiredWorkspaceCache.hpp"
#include "../src/RoundTransitionReservation.hpp"
#include <vector>
#include <iostream>
#include <sstream>
using namespace sc6::research;
static void simulate(uint64_t outbound,uint64_t inbound,uint64_t offset,uint64_t preparation,bool reverse,bool drop,bool control_loss=false){
    CombatStartAgreement::Offer a{reverse?2u:1u,reverse?1u:2u,100,77,3,4,500,1,1,3,7200};auto b=a;
    b.sender=a.recipient;b.recipient=a.sender;b.ticket=200;b.slot=2;b.delay=2;
    CombatStartExchange left,right;CombatStartMailbox lm(GetCurrentThreadId()),rm(GetCurrentThreadId());
    assert(left.attach(lm) && right.attach(rm));assert(left.begin(22,a) && right.begin(11,b));
    assert(left.report_local_hold(a.ticket));
    struct Transit{uint64_t at;bool left;CombatStartPacket::Bytes bytes;};std::vector<Transit> queue;
    CombatStartSendCadence lc,rc;uint64_t lr=0,rr=0;unsigned discarded=0,lost_controls=0;
    for(uint64_t time=1000000;time<4000000;time+=1000){
        if(time==1000000+preparation)assert(right.report_local_hold(b.ticket));
        for(size_t n=0;n<queue.size();){
            if(queue[n].at>time){++n;continue;}
            auto p=queue[n];queue.erase(queue.begin()+n);
            const auto kind=unsigned(CombatStartPacket::get(p.bytes,8,4));
            if(control_loss && kind>=4 && !(lost_controls&(1u<<kind))){lost_controls|=1u<<kind;continue;}
            assert(p.left?left.receive(22,true,p.bytes,p.bytes.size(),time):right.receive(11,true,p.bytes,p.bytes.size(),time+offset));
        }
        auto l=left.outgoing(time),r=right.outgoing(time+offset);
        if(lc.due(l,time/1000,left.completed())){left.sent(l,time);
            if(drop && CombatStartPacket::get(l,8,4)<4 && discarded++<3){}else queue.push_back({time+outbound,false,l});}
        if(rc.due(r,time/1000,right.completed())){right.sent(r,time+offset);queue.push_back({time+inbound,true,r});}
        CombatStartAgreement agreement;
        if(lm.take(agreement,time)){lr=time;assert(agreement.agreed_delay()==2 && agreement.consume(a));}
        if(rm.take(agreement,time+offset)){rr=time;assert(agreement.agreed_delay()==2 && agreement.consume(b));}
        if(lr && rr)break;
        assert(!left.failed() && !right.failed());
    }
    assert(lr && rr);
    const auto skew=lr>rr?lr-rr:rr-lr;
    // Offset estimation cannot distinguish path asymmetry from clock offset.
    const auto asymmetry=outbound>inbound?outbound-inbound:inbound-outbound;
    assert(skew<=asymmetry/2+1000);
}
struct CachedWorkspace {bool reusable=true;bool prepare_for_reuse(){return reusable;}};
// Reproduce an ordered-stream commit delay beyond the release deadline. The
// independent immediate copy must survive loss, duplicates and late reliable
// copies without changing targets or weakening expiry validation.
static void commit_delivery(bool immediate_enabled){
    CombatStartExchange l,r;CombatStartMailbox lm(GetCurrentThreadId()),rm(GetCurrentThreadId());
    CombatStartAgreement::Offer a{1,2,100,77,3,4905,5025,1,1,3,7200},b=a;
    b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;
    assert(l.attach(lm) && r.attach(rm) && l.begin(22,a) && r.begin(11,b));
    assert(l.report_local_hold(100) && r.report_local_hold(200));
    struct Packet{uint64_t at;bool to_left;CombatStartPacket::Bytes bytes;};std::vector<Packet> q;
    CombatStartSendCadence lc,rc;uint64_t lr=0,rr=0;unsigned dropped=0;
    for(uint64_t t=1000000;t<2200000;t+=1000){
        for(size_t i=0;i<q.size();){if(q[i].at>t){++i;continue;}auto p=q[i];q.erase(q.begin()+i);
            auto ok=p.to_left?l.receive(22,true,p.bytes,112,t):r.receive(11,true,p.bytes,112,t+9000000);
            if(immediate_enabled)assert(ok);
        }
        auto lp=l.outgoing(t),rp=r.outgoing(t+9000000);
        auto transmit=[&](CombatStartExchange& ex,const auto& bytes,bool to_left,uint64_t now){
            const auto kind=CombatStartPacket::get(bytes,8,4);
            ex.transmit(bytes,[&](int mode){
                if(mode==1 && kind>=4 && !immediate_enabled)return true;
                if(mode==1 && kind==6 && dropped++<2)return true;
                const auto delay=(mode==2 && kind==6)?300000u:45000u;
                q.push_back({t+delay,to_left,bytes});return true;
            },now);
        };
        if(lc.due(lp,t/1000,l.completed()))transmit(l,lp,false,t);
        if(rc.due(rp,t/1000,r.completed()))transmit(r,rp,true,t+9000000);
        CombatStartAgreement agreement;if(lm.take(agreement,t))lr=t;if(rm.take(agreement,t+9000000))rr=t;
        if(!immediate_enabled && r.failed()){assert(lr && !rr);return;}
        if(immediate_enabled)assert(!l.failed() && !r.failed());
    }
    assert(immediate_enabled && lr && rr && lr==rr);
    std::ostringstream report;l.report(report);
    assert(report.str().find("_delivery_6=")!=std::string::npos);
}
static void retained_timing_rounds(bool drop_controls=false,bool prepare_transition=false,bool reserve=false){
    CombatStartExchange left[3],right[3];
    CombatStartMailbox lm0(GetCurrentThreadId()),lm1(GetCurrentThreadId()),lm2(GetCurrentThreadId()),rm0(GetCurrentThreadId()),rm1(GetCurrentThreadId()),rm2(GetCurrentThreadId());
    CombatStartMailbox* lm[]={&lm0,&lm1,&lm2};CombatStartMailbox* rm[]={&rm0,&rm1,&rm2};
    CombatStartAgreement::Offer a{1,2,100,77,3,4,500,1,1,3,7200},b=a;
    b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;
    struct Transit{uint64_t at;bool to_left;CombatStartPacket::Bytes bytes;};std::vector<Transit> queue;
    CombatStartSendCadence lc,rc;uint64_t release[3][2]{};unsigned round=0,lost_controls=0;
    constexpr uint64_t second_round=31000000;
    std::unique_ptr<CombatRoundPreparation> lp,rp;
    for(uint64_t time=1000000;time<second_round+31000000;time+=1000){
        if(prepare_transition && (time==second_round-500000 || time==second_round+29500000)){
            auto pa=a,pb=b;++pa.round;++pa.event;++pa.ticket;++pb.round;++pb.event;++pb.ticket;
            lp=std::make_unique<CombatRoundPreparation>(22,pa,time,reserve);
            rp=std::make_unique<CombatRoundPreparation>(11,pb,time+9000000,reserve);
        }
        if(time==1000000 || time==second_round || time==second_round+30000000){
            round=time==1000000?0u:(time==second_round?1u:2u);lost_controls=0;
            if(round){a.round++;a.event++;a.ticket++;b.round++;b.event++;b.ticket++;}
            assert(left[round].attach(*lm[round]) && right[round].attach(*rm[round]));
            assert(left[round].begin(22,a,round?&left[round-1]:nullptr));
            assert(right[round].begin(11,b,round?&right[round-1]:nullptr));
            assert(left[round].report_local_hold(a.ticket) && right[round].report_local_hold(b.ticket));
            if(round && reserve){
                assert(left[round].prime_reservation(lp.get(),time));
                assert(right[round].prime_reservation(rp.get(),time+9000000));
                assert(left[round].completed() && right[round].completed());
            }
        }
        for(size_t n=0;n<queue.size();){
            if(queue[n].at>time){++n;continue;}
            const auto p=queue[n];queue.erase(queue.begin()+n);
            const auto kind=unsigned(CombatStartPacket::get(p.bytes,8,4));
            if(kind>=7){
                if(drop_controls && !(lost_controls&(1u<<kind))){lost_controls|=1u<<kind;continue;}
                const auto decoded=CombatStartPacket::decode(p.bytes,p.bytes.size());assert(decoded);
                const auto active=CombatStartPacket::decode(lp->outgoing(),112);assert(active);
                const auto accepted=p.to_left?lp->receive(22,*decoded,time):rp->receive(11,*decoded,time+9000000);
                assert(accepted==(decoded->offer.event==active->offer.event));continue;
            }
            if(round && drop_controls && kind>=4 && !(lost_controls&(1u<<kind))){lost_controls|=1u<<kind;continue;}
            assert(p.to_left?left[round].receive(22,true,p.bytes,p.bytes.size(),time):
                right[round].receive(11,true,p.bytes,p.bytes.size(),time+9000000));
        }
        combat_round_preparation=lp.get();auto l=left[round].outgoing(time);
        combat_round_preparation=rp.get();auto r=right[round].outgoing(time+9000000);
        if(lp && lp->due(time))queue.push_back({time+45000,false,lp->outgoing()});
        if(rp && rp->due(time+9000000))queue.push_back({time+45000,true,rp->outgoing()});
        if(lc.due(l,time/1000,left[round].completed())){left[round].sent(l,time);queue.push_back({time+45000,false,l});}
        if(rc.due(r,time/1000,right[round].completed())){right[round].sent(r,time+9000000);queue.push_back({time+45000,true,r});}
        CombatStartAgreement agreement;
        if(lm[round]->take(agreement,time))release[round][0]=time;
        if(rm[round]->take(agreement,time+9000000))release[round][1]=time;
    }
    assert(release[0][0] && release[0][1] && release[1][0] && release[1][1]);
    assert(release[1][0]==release[1][1]);
    assert(release[2][0] && release[2][0]==release[2][1]);
    const auto cold=release[0][0]-1000000,warm=release[1][0]-second_round;
    if(!drop_controls)assert(warm<cold); // Same 90ms connection and safety margin, less measurement waiting.
    if(prepare_transition)assert(warm<275000); // Intent negotiated while native transition runs.
    if(reserve)assert(warm==0 && release[2][0]==second_round+30000000);
    std::ostringstream report;left[1].report(report);
    assert(report.str().find("start_sync_round_5_carried_samples=0\n")==std::string::npos);
    auto changed=a;changed.event++;changed.round++;changed.build++;
    CombatStartExchange incompatible;assert(!incompatible.begin(22,changed,&left[1]));
    combat_round_preparation=nullptr;
    std::cout<<"90ms RTT simulated cold hold="<<cold<<"us; transition="<<prepare_transition<<" next hold="<<warm<<"us\n";
}
// Both native owners reserved the same boundary, but a final transition ACK
// was lost on one or both routes. Exercise the real held-packet fallback in
// both leader directions, with no cached clock samples and delayed local hold.
static void asymmetric_reservation(unsigned ack_mask,unsigned delay){
    CombatStartAgreement::Offer a{1,2,100,77,5,10284,10404,1,delay,3,7200},b=a;
    b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;b.delay=2-delay;
    CombatRoundPreparation lp(22,a,1000000,true),rp(11,b,1000000,true);
    auto l=CombatStartPacket::decode(lp.outgoing(),112),r=CombatStartPacket::decode(rp.outgoing(),112);
    assert(l && r && lp.receive(22,*r,1000001) && rp.receive(11,*l,1000001));
    l=CombatStartPacket::decode(lp.outgoing(),112);r=CombatStartPacket::decode(rp.outgoing(),112);
    if(ack_mask&1)assert(lp.receive(22,*r,1000002));
    if(ack_mask&2)assert(rp.receive(11,*l,1000002));
    CombatStartExchange le,re;CombatStartMailbox lm(GetCurrentThreadId()),rm(GetCurrentThreadId());
    assert(le.attach(lm) && re.attach(rm) && le.begin(22,a) && re.begin(11,b));
    assert(le.report_local_hold(100));
    assert(le.prime_reservation(&lp,1000003)==bool(ack_mask&1));
    assert(!re.prime_reservation(&rp,1000003)); // Right has not physically held yet.
    struct Transit{uint64_t at;bool to_left;CombatStartPacket::Bytes bytes;};std::vector<Transit> q;
    CombatStartSendCadence lc,rc;bool lt=false,rt=false;unsigned dropped=0;
    for(uint64_t t=1010000;t<2000000;t+=1000){
        if(t==1110000)assert(re.report_local_hold(200));
        for(size_t i=0;i<q.size();){if(q[i].at>t){++i;continue;}auto p=q[i];q.erase(q.begin()+i);
            combat_round_preparation=p.to_left?&lp:&rp;
            assert(p.to_left?le.receive(22,true,p.bytes,112,t):re.receive(11,true,p.bytes,112,t));
        }
        combat_round_preparation=&lp;auto lb=le.outgoing(t);
        combat_round_preparation=&rp;auto rb=re.outgoing(t);
        if(lc.due(lb,t/1000,le.completed())){le.sent(lb,t);q.push_back({t+45000,false,lb});}
        if(rc.due(rb,t/1000,re.completed())){re.sent(rb,t);
            if(CombatStartPacket::get(rb,8,4)!=3 || dropped++>=2){q.push_back({t+45000,true,rb});q.push_back({t+47000,true,rb});}}
        CombatStartAgreement agreement;
        if(lm.take(agreement,t)){lt=true;assert(agreement.activation_ready() && agreement.agreed_delay()==(std::max)(delay,2-delay));}
        if(rm.take(agreement,t)){rt=true;assert(t>=1110000 && agreement.activation_ready());}
        if(t<1110000)assert(!re.completed() && !rt);
        assert(!le.failed() && !re.failed());
        if(lt && rt)break;
    }
    assert(lt && rt);
    std::ostringstream ls,rs;le.report(ls);re.report(rs);
    if(!(ack_mask&1))assert(ls.str().find("_held_reservation_recovered=1\n")!=std::string::npos);
    if(!(ack_mask&2))assert(rs.str().find("_held_reservation_recovered=1\n")!=std::string::npos);
    combat_round_preparation=nullptr;
}
static void held_reservation_guards(){
    CombatStartAgreement::Offer a{1,2,100,77,5,10284,10404,1,1,3,7200},b=a;
    b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;
    CombatRoundPreparation p(22,a,1000000,true),intent(22,a,1000000,false);
    assert(p.held_reservation(a,b,100,1000001));
    assert(!intent.held_reservation(a,b,100,1000001));
    assert(!p.held_reservation(a,b,101,1000001));
    assert(!p.held_reservation(a,b,100,999999));
    assert(!p.held_reservation(a,b,100,62000000));
    for(unsigned f=0;f<11;++f){auto bad=b;
        switch(f){case 0:++bad.sender;break;case 1:++bad.recipient;break;case 2:bad.ticket=0;break;
        case 3:++bad.build;break;case 4:++bad.round;break;case 5:++bad.event;break;
        case 6:++bad.first_frame;break;case 7:bad.slot=1;break;case 8:bad.delay=3;break;
        case 9:bad.rollback=4;break;case 10:--bad.records;break;}
        assert(!p.held_reservation(a,bad,100,1000001));
    }
    auto offer=CombatStartPacket{b,9,0,{}};assert(p.receive(22,offer,1000001));
    auto changed=b;++changed.ticket;assert(!p.held_reservation(a,changed,100,1000002));
    ++offer.offer.delay;assert(!p.receive(22,offer,1000002));
    assert(!p.held_reservation(a,b,100,1000003)); // A conflicted preparation stays failed.
}
int main(){
    for(unsigned mask:{1u,2u,0u})for(unsigned delay=0;delay<=2;++delay)asymmetric_reservation(mask,delay);
    held_reservation_guards();
    // Round one has no previous exchange. Exact intro reservations must still
    // supply bilateral agreement, but never substitute for the local hold.
    for(unsigned delay=0;delay<=2;++delay){
        CombatStartAgreement::Offer a{1,2,100,77,1,4,500,1,delay,3,7200},b=a;
        b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;b.delay=2-delay;
        CombatRoundPreparation lp(22,a,1000000,true),rp(11,b,1000000,true);
        const auto l=CombatStartPacket::decode(lp.outgoing(),112),r=CombatStartPacket::decode(rp.outgoing(),112);
        assert(l && r && lp.receive(22,*r,1000001) && rp.receive(11,*l,1000001));
        const auto la=CombatStartPacket::decode(lp.outgoing(),112),ra=CombatStartPacket::decode(rp.outgoing(),112);
        assert(lp.receive(22,*ra,1000002) && rp.receive(11,*la,1000002));
        CombatStartMailbox lm(GetCurrentThreadId()),rm(GetCurrentThreadId());CombatStartExchange le,re;
        assert(le.attach(lm) && re.attach(rm) && le.begin(22,a) && re.begin(11,b));
        assert(!le.prime_reservation(&lp,1000003) && !le.completed());
        assert(le.report_local_hold(a.ticket) && re.report_local_hold(b.ticket));
        assert(le.prime_reservation(&lp,1000004) && re.prime_reservation(&rp,1000004));
        CombatStartAgreement agreed;
        assert(lm.take(agreed,1000004) && agreed.agreed_delay()==(std::max)(delay,2-delay));
        assert(rm.take(agreed,1000004) && agreed.agreed_delay()==(std::max)(delay,2-delay));
        // A stale or different match/frontier cannot prime first-round activation.
        for(unsigned fault=0;fault<5;++fault){
            auto wrong=a;
            if(fault==0)++wrong.first_frame;if(fault==1)++wrong.event;
            if(fault==2)++wrong.ticket;if(fault==3)++wrong.build;
            CombatStartMailbox m(GetCurrentThreadId());CombatStartExchange e;
            assert(e.attach(m) && e.begin(22,wrong) && e.report_local_hold(wrong.ticket));
            assert(!e.prime_reservation(&lp,fault==4?62000000:1000005) && !e.completed());
        }
    }
    for(unsigned duration:{3u,120u,156u}){
        RoundTransitionReservation reservation;
        for(unsigned elapsed=0;elapsed<duration;++elapsed){
            assert(reservation.observe({12,4,100+elapsed,900+elapsed,7,elapsed,duration,99}));
            if(elapsed==1 && duration>=30)assert(reservation.reserve());
        }
        if(duration>=30){
            assert(reservation.native_step_allowed(100+duration-1));
            assert(!reservation.native_step_allowed(100+duration));
            assert(!reservation.native_step_allowed(101+duration));
            assert(reservation.may_wait(12,4,99,100+duration-1,900+duration-1,7,2000,1000));
            assert(!reservation.may_wait(12,4,99,100+duration,900+duration-1,7,2000,1000));
            assert(reservation.may_wait(12,4,99,100+duration-1,901+duration,7,2000,1000));
            assert(!reservation.may_wait(12,4,99,100+duration-1,899+duration-1,7,2000,1000));
            assert(!reservation.may_wait(12,4,99,100+duration-1,900+duration-1,7,11000,1000));
            assert(!reservation.may_wait(12,4,99,100+duration-1,900+duration-1,8,2000,1000));
            assert(!reservation.may_wait(13,4,99,100+duration-1,900+duration-1,7,2000,1000));
            assert(reservation.wait_rejections(12,4,99,100+duration,900+duration-1,7,2000,1000)==32);
            assert(reservation.wait_rejections(13,4,99,100+duration-1,900+duration-1,7,2000,1000)==2);
            assert(reservation.wait_rejections(12,5,99,100+duration-1,900+duration-1,7,2000,1000)==4);
            assert(reservation.wait_rejections(12,4,100,100+duration-1,900+duration-1,7,2000,1000)==8);
            assert(reservation.wait_rejections(12,4,99,100+duration-1,900+duration-1,8,2000,1000)==16);
            assert(reservation.wait_rejections(12,4,99,100+duration-1,899+duration-1,7,2000,1000)==64);
            assert(reservation.wait_rejections(12,4,99,100+duration-1,900+duration-1,7,999,1000)==128);
            assert(reservation.wait_rejections(12,4,99,100+duration-1,900+duration-1,7,11000,1000)==256);
            assert(reservation.wait_rejections(13,5,100,100+duration,899+duration-1,8,11000,1000)==(2|4|8|16|32|64|256));
            assert(reservation.arrives(12,4,99,100+duration,900+duration,7));
            assert(reservation.arrives(12,4,99,100+duration,902+duration,7)); // Native clock may get ahead of battle updates.
            assert(!reservation.arrives(12,4,99,101+duration,900+duration,7));
            assert(!reservation.arrives(12,4,99,100+duration,900+duration,8));
            assert(!reservation.arrives(13,4,99,100+duration,900+duration,7));
            assert(!reservation.arrives(12,4,100,100+duration,900+duration,7));
        }else assert(!reservation.can_reserve());
    }
    {
        RoundTransitionReservation r;
        RoundTransitionReservation::Sample s{12,4,100,900,7,0,120,99};
        assert(r.observe(s) && r.observe(s) && !r.can_reserve()); // Duplicate polls are not updates.
        ++s.battle;++s.clock;++s.elapsed;assert(r.observe(s) && r.reserve());
        ++s.duration;assert(!r.observe(s) && r.failed()); // Intro change invalidates a published promise.
        RoundTransitionReservation overflow;s.battle=UINT32_MAX;
        assert(!overflow.observe(s) && !overflow.can_reserve());
    }
    {
        RoundTransitionReservation r;
        assert(r.observe({12,4,100,900,7,0,120,99}));
        assert(r.observe({12,4,101,903,7,1,120,99}) && r.reserve());
        assert(r.observe({12,4,103,904,7,3,120,99}));
        assert(r.arrives(12,4,99,220,1025,7)); // Outer clock and battle counters need not have a fixed offset.
    }
    {
        CombatStartAgreement::Offer a{1,2,100,77,3,4,500,1,0,3,7200},b=a;
        b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;b.delay=2;
        CombatRoundPreparation p(22,a,1000000,true);
        CombatStartAgreement agreement;assert(agreement.begin(a));
        CombatStartPacket packet{b,9,0,{}};
        assert(p.receive(22,packet,1000001));
        assert(!p.import_reservation(a,1000002,agreement));
        packet.kind=10;packet.echo=100;
        assert(!p.receive(99,packet,1000003));
        assert(p.receive(22,packet,1000003));
        auto wrong=a;++wrong.first_frame;assert(!p.import_reservation(wrong,1000004,agreement));
        assert(p.import_reservation(a,1000004,agreement));
        CombatStartMailbox mailbox(GetCurrentThreadId());
        assert(!agreement.activation_ready() && !mailbox.publish(agreement)); // No physical local hold yet.
        assert(!agreement.local_held(101));assert(agreement.local_held(100));
        assert(agreement.activation_ready() && agreement.agreed_delay()==2);
        assert(mailbox.publish(agreement));CombatStartAgreement taken;
        assert(mailbox.take(taken,1000004) && taken.consume(a));
        assert(!mailbox.take(taken,1000005));
        assert(!p.import_reservation(a,62000000,agreement));
    }
    commit_delivery(false);commit_delivery(true);
    {
        CombatStartExchange exchange;CombatStartMailbox mailbox(GetCurrentThreadId());
        CombatStartAgreement::Offer a{1,2,100,77,3,4,500,1,1,3,7200};
        assert(exchange.attach(mailbox) && exchange.begin(22,a));
        auto offer=exchange.outgoing(1000000);unsigned calls=0;
        assert(!exchange.transmit(offer,[&](int mode){assert(mode==1);++calls;return false;},1000000));
        assert(calls==1 && !exchange.completed());
        std::ostringstream report;exchange.report(report);
        assert(report.str().find("_delivery_1=0,0,0,0,0,0,0,0,0,1")!=std::string::npos);
    }
    retained_timing_rounds();
    retained_timing_rounds(true);
    retained_timing_rounds(false,true);
    retained_timing_rounds(true,true);
    retained_timing_rounds(false,true,true);
    retained_timing_rounds(true,true,true);
    {
        CombatStartAgreement::Offer a{1,2,100,77,3,4,500,1,1,3,7200},b=a;
        b.sender=2;b.recipient=1;b.ticket=200;b.slot=2;b.first_frame=0;
        CombatRoundPreparation preparation(22,a,1000000);
        CombatStartPacket p{b,7,0,{}};
        assert(!preparation.seed(a,1000001));
        assert(preparation.receive(22,p,1000001));
        assert(!preparation.seed(a,1000002)); // An offer is not an acknowledgment.
        p.kind=8;p.echo=100;assert(preparation.receive(22,p,1000003));
        assert(preparation.seed(a,1000004)==200);
        auto changed=a;changed.event++;assert(!preparation.seed(changed,1000004));
        changed=a;changed.delay++;assert(!preparation.seed(changed,1000004));
        assert(!preparation.seed(a,62000000) && !preparation.seed(a,999999));
        assert(!preparation.receive(99,p,1000004));
        p.offer.first_frame=500;assert(!preparation.receive(22,p,1000004));p.offer.first_frame=0;
        p.echo=101;assert(!preparation.receive(22,p,1000004));p.echo=100;
        p.timing.target=2000000;assert(!preparation.receive(22,p,1000004));p.timing={};
        // A transition intent cannot authorize activation or publish a mailbox.
        CombatStartExchange exchange;CombatStartMailbox mailbox(GetCurrentThreadId());
        assert(exchange.attach(mailbox) && exchange.begin(22,a));
        assert(!exchange.receive(22,true,p.encode(),112,1000004) && !exchange.completed());
        p.offer.delay=2;assert(!preparation.receive(22,p,1000005));assert(!preparation.seed(a,1000006));
    }
    CombatStartSync prior;prior.leader=true;
    for(uint64_t t=1000000;t<1030000;t+=10000){
        prior.sent({t,0,0,0},3,t);
        assert(prior.receive({t+9030000,t,t+9030000,0},3,t+60000,true));
    }
    assert(prior.kind(1100000,true,3)==4);
    const auto target=prior.local_target,margin_before=prior.release_margin;const auto frozen_offset=prior.peer_offset;
    prior.sent({2000000,0,0,0},4,2000000);
    assert(prior.receive({11040000,2000000,11040000,0},3,2080000,true));
    assert(prior.local_target==target && prior.peer_offset==frozen_offset && prior.release_margin==margin_before);
    CombatStartSync fresh;fresh.leader=true;fresh.inherit_timing(prior,2100000);
    assert(fresh.carried_samples==4 && fresh.carried_age_us==20000);
    assert(!fresh.local_target && !fresh.committed && !fresh.commit_sent && !fresh.peer_sent);
    assert(fresh.kind(2100000,false,3)==3); // Timing never substitutes for fresh bilateral readiness.
    assert(fresh.kind(2100000,true,3)==4 && fresh.worst_rtt==80000);
    CombatStartSync stale;stale.leader=true;stale.inherit_timing(prior,8000000);
    assert(!stale.carried_samples && stale.kind(8000000,true,3)==3);
    CombatStartSync backwards;backwards.inherit_timing(prior,900000);assert(!backwards.samples);
    CombatStartSync expires;expires.leader=true;expires.inherit_timing(prior,2100000);
    assert(expires.kind(8000000,true,3)==3 && !expires.samples);
    RetiredWorkspaceCache<CachedWorkspace> cache;
    assert(!cache.prepare(0) && cache.prepare(123) && cache.prepare(123) && !cache.prepare(456));
    assert(cache.take(123) && !cache.take(123));
    auto first=std::make_unique<CachedWorkspace>();auto* original=first.get();
    assert(!cache.put(first,123,false) && first.get()==original);
    first->reusable=false;assert(!cache.put(first,123,true));
    first->reusable=true;assert(cache.put(first,123,true) && !first);
    assert(!cache.take(456));
    auto second=std::make_unique<CachedWorkspace>();assert(cache.put(second,123,true) && !second);
    auto reused=cache.take(123);assert(reused.get()==original && !cache.take(123));
    CombatStartSync margin;margin.leader=true;margin.samples=3;margin.best_rtt=85000;margin.worst_rtt=95000;
    assert(margin.kind(1000000,true,3)==4 && margin.release_margin==192500);
    CombatStartSync lan;lan.leader=true;lan.samples=3;lan.best_rtt=1000;
    assert(lan.kind(1000000,true,3)==4 && lan.release_margin==100000);

    CombatStartSendCadence cadence;CombatStartPacket packet;
    auto bytes=packet.encode();assert(cadence.due(bytes,0,false));
    packet.timing.sent=1000;assert(!cadence.due(packet.encode(),1,false));
    packet.kind=4;packet.timing.target=50000;assert(cadence.due(packet.encode(),1,false));
    assert(cadence.due(packet.encode(),11,false));
    for(auto delay:{1000ull,33000ull,70000ull})for(auto offset:{0ull,9000000ull})for(bool reverse:{false,true})
        for(auto prepare:{0ull,30000ull,300000ull})simulate(delay,delay,offset,prepare,reverse,true);
    simulate(30000,40000,9000000,30000,false,false);
    for(auto delay:{1000ull,45000ull,70000ull})simulate(delay,delay,9000000,30000,false,false,true);
    CombatStartSync s;s.leader=true;
    // Peer processing time is removed; duplicate echoes cannot create samples.
    s.sent({1000000,0,0,0},3,1000000);
    CombatStartSync::Wire w{10040000,1000000,10030000,0};
    assert(s.receive(w,3,1070000,true));assert(s.samples==1 && s.best_rtt==60000 && s.peer_offset==9000000);
    assert(s.receive(w,3,1070000,true) && s.samples==1);
    assert(s.kind(1070000,true,3)==3); // one sample never releases either peer
    assert(!s.receive({1,2,0,0},3,1000,true));
    CombatStartSync follower;
    assert(!follower.receive({1000,0,0,2000},4,1000,false)); // no bilateral hold
    assert(follower.receive({1000,0,0,2000},4,1000,true));
    assert(!follower.receive({1100,0,0,2001},6,1100,true)); // target changed
    assert(!follower.can_publish());
    assert(!follower.receive({2100,0,0,2000},6,2100,true));assert(follower.failed); // missed release
    CombatStartSync leader;leader.leader=true;leader.samples=3;leader.best_rtt=60000;
    assert(leader.kind(1000000,true,3)==4);
    assert(!leader.receive({2000000,0,0,leader.peer_target},5,leader.local_target,true));assert(leader.failed);
    CombatStartSync unsent;unsent.leader=true;unsent.samples=3;unsent.best_rtt=60000;
    assert(unsent.kind(1000000,true,3)==4);
    assert(unsent.receive({1001000,0,0,unsent.peer_target},5,1001000,true));
    assert(!unsent.can_publish()); // Ack alone does not release the coordinator.
    unsent.sent(unsent.wire(unsent.local_target,6),6,unsent.local_target);
    assert(unsent.failed && !unsent.can_publish()); // Failed sends cannot commit after the deadline.
    std::cout<<"Synchronized start: clock offsets, roles, preparation skew, loss, asymmetry, expiry, and target identity passed\n";
}
