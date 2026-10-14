#pragma once
// Executes the production correction algorithm with simulated capture/load and
// update operations. This is not a test of the SC6 serializer itself.
namespace bounded_transaction_test {
using namespace sc6::research;
inline unsigned frame,value,next_window,fail_at=9999,updates,releases[2];
inline unsigned throw_release_mask{};
inline bool attack_fixture{},combat_fixture{},ineffective_input{};
inline unsigned truth_values[4]={0,11,12,23};
inline std::vector<unsigned> events;
inline bool event(unsigned id){events.push_back(id);return id!=fail_at;}
struct Source {bool available()const{return true;}};
struct Pool {
    std::array<std::unique_ptr<NativeBoundarySnapshot>,4> states;
    Pool(){for(auto& s:states)s=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::idle);}
    template<class F> bool visit(unsigned i,F f){return i<4 && f(*states[i]);}
};
struct Hop {
    std::unique_ptr<FullStateProbe> probe=std::make_unique<FullStateProbe>();
    bool replay_delivered{};
    bool replay_step(const NativeBoundarySnapshot& source,uintptr_t,uintptr_t argument,bool){
        if(combat_fixture){std::array<std::byte,40> envelope{};assert(FullStateProbe::input_bytes(argument,envelope));assert(envelope==source.data->delayed_inputs);}
        const unsigned i=frame-101;
        return event(200+i) && value==(combat_fixture?truth_values[i+1]:i+1);
    }
};
struct Window {
    unsigned id=next_window++,count_{},capture_stage{},last_hop{};uintptr_t base_=1;bool fault_{},loaded{};
    Pool pool_;std::array<unsigned,4> handles_{0,1,2,3};std::array<RollbackBoundaryKey,4> keys_{};
    std::array<std::unique_ptr<Hop>,3> hops_;
    Window(){for(auto& h:hops_)h=std::make_unique<Hop>();}
    bool dependencies_valid()const{return true;}
    bool rewind_started()const{return loaded;}
    bool any_restore_invoked()const{return loaded;}
    bool capture(const RollbackBoundaryKey& key,uintptr_t,uintptr_t,NativeBoundarySnapshot::Mode mode,bool,Source*){
        if(attack_fixture)assert(mode==NativeBoundarySnapshot::Mode::general_combat);
        capture_stage=1;if(!event(100*id+10+count_))return false;
        assert(key.frame==frame);keys_[count_]=key;
        std::memcpy(pool_.states[count_]->data->measurements[0].fields[0].data()+8,&value,4);
        ++count_;return true;
    }
    bool prepare(unsigned i){return event(100*id+i);}
    bool validate_before_load(unsigned){return true;}
    bool restore(unsigned i){
        if(!event(100*id+20+i))return false;
        loaded=true;frame=keys_[i].frame;
        std::memcpy(&value,pool_.states[i]->data->measurements[0].fields[0].data()+8,4);return true;
    }
    bool rewind(const RollbackBoundaryKey& live){return RollbackWindowPlan::execute(keys_,count_,live,*this)==RollbackWindowPlan::Result::restored;}
    bool release_owned_dependencies(){++releases[id];if(throw_release_mask&(1u<<id))throw id;return event(300+id);}
};
struct Update {
    bool invoked{};
    bool run(SingleUpdateCall::Routine,uintptr_t arg,volatile uint32_t*,uint32_t expected,DWORD){
        assert(frame==expected);invoked=true;
        std::array<std::byte,40> input{};assert(FullStateProbe::input_bytes(arg,input));
        unsigned p1{},p2{};std::memcpy(&p1,input.data(),4);std::memcpy(&p2,input.data()+8,4);
        if(attack_fixture){assert((p1|p2)==(updates==0?1u:0u));}
        value+=1+(!ineffective_input && (p1||p2)?10:0);++frame;
        return event(400+updates++);
    }
};
inline void original(uintptr_t){}
inline void run(){
    using Backend=OwnedPredictionCorrectionBackend<Window,Update,Source>;
    const unsigned failures[]={9999,0,1,2,22,21,20,110,111,112,113,100,101,102,122,121,120,200,201,202,400,401,402,403,404,405,300,301};
    for(bool attack:{false,true})for(unsigned slot:{1u,2u})for(unsigned failure:failures){
        attack_fixture=attack;
        frame=103;value=3;next_window=updates=0;releases[0]=releases[1]=0;fail_at=failure;events.clear();
        auto b=std::make_unique<Backend>();b->attack_fixture=attack;Source source;typename Backend::Frames frames{};
        b->clean.count_=4;
        for(unsigned i=0;i<4;++i){
            b->clean.keys_[i]={1,2,3,1,4,100+i,slot,GetCurrentThreadId()};
            std::memcpy(b->clean.pool_.states[i]->data->measurements[0].fields[0].data()+8,&i,4);
            if(i<3){frames[i].key=b->clean.keys_[i];frames[i].confirmed=true;}
        }
        const bool ok=b->run_confirmed(frames,b->clean.keys_[3],1,source,original);
        assert(ok==(failure==9999));
        if(ok){
            const std::vector<unsigned> expected={0,1,2,22,21,20,110,400,111,401,112,402,113,100,101,102,122,121,120,403,200,404,201,405,202,301,300};
            assert(events==expected);assert(frame==103 && value==3 && b->predicted_updates==3 && b->corrected_updates==3 && b->state_changed && b->verified && b->retired && !b->restart_required);}
        else {assert(!b->verified);if(b->clean.loaded)assert(b->restart_required);}
        const auto count=events.size();assert(!b->run_confirmed(frames,b->clean.keys_[3],1,source,original) && events.size()==count);
        // Cleanup must drain both windows, including when one drain rejects.
        b->release();assert(releases[0] && releases[1]);
        if(failure==300 || failure==301)assert(!b->retired && b->restart_required);
    }
    for(bool ineffective:{false,true})for(unsigned slot:{1u,2u}){
        ineffective_input=ineffective;truth_values[1]=ineffective?1:11;truth_values[2]=ineffective?2:12;truth_values[3]=ineffective?3:23;
        combat_fixture=true;attack_fixture=false;frame=103;value=23;next_window=updates=0;releases[0]=releases[1]=0;fail_at=9999;events.clear();
        auto b=std::make_unique<Backend>();b->combat_inputs=true;Source source;typename Backend::Frames frames{};b->clean.count_=4;
        for(unsigned i=0;i<4;++i){
            b->clean.keys_[i]={1,2,3,1,4,100+i,slot,GetCurrentThreadId()};
            std::memcpy(b->clean.pool_.states[i]->data->measurements[0].fields[0].data()+8,&truth_values[i],4);
            if(i<3){frames[i].key=b->clean.keys_[i];frames[i].confirmed=true;
                const unsigned remote=3-slot,bits=i==0?1u:i==2?0x400u:0u;
                for(unsigned word:{2*(remote-1),2*(remote-1)+1,4+remote-1})frames[i].local[word]=frames[i].peer[word]=bits;
                std::memcpy(b->clean.pool_.states[i]->data->delayed_inputs.data(),frames[i].local.data(),40);
            }
        }
        assert(b->run_confirmed(frames,b->clean.keys_[3],1,source,original)==!ineffective);
        assert(b->predicted_updates==3 && b->corrected_updates==3 && b->state_changed==!ineffective && value==truth_values[3] && b->retired && !b->restart_required);
    }
    combat_fixture=false;ineffective_input=false;
    for(unsigned mask:{1u,2u,3u}){
        next_window=0;releases[0]=releases[1]=0;fail_at=9999;throw_release_mask=mask;
        auto b=std::make_unique<Backend>();
        assert(!b->release() && !b->retired);
        assert(releases[0]==1 && releases[1]==1);
        // A later owner-thread cleanup can finish, but retirement is never
        // published while either independent drain is incomplete.
        throw_release_mask=0;assert(b->release() && b->retired);
        assert(releases[0]==2 && releases[1]==2);
        assert(b->release() && releases[0]==2 && releases[1]==2);
    }
    std::cout<<"PASS: bounded production transaction, both slots, three-frame correction and capture/load/update/compare/cleanup faults\n";
}
}
