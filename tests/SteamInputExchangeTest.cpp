#include "../src/SteamInputExchange.hpp"
#include "../src/SteamHandshakeProtocol.hpp"
#include <cassert>
#include <iostream>
#include <memory>
using namespace sc6::steam_inputs;
static sc6::observation::Shared* preparation_fixture;
static unsigned aborts;
int main(){
    {
        using namespace sc6;
        const auto handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(observation::Shared),
            observation::mapping_name(GetCurrentProcessId()).c_str());
        assert(handle && GetLastError()!=ERROR_ALREADY_EXISTS);
        auto shared=static_cast<observation::Shared*>(MapViewOfFile(handle,FILE_MAP_WRITE,0,0,sizeof(observation::Shared)));assert(shared);
        *shared={};shared->signature=observation::magic;shared->schema=observation::version;
        shared->pid=GetCurrentProcessId();shared->frequency=10000000;
        auto startup=std::make_unique<Capture>();
        assert(startup->prepare_owned(77)==Capture::OwnedStart::unavailable);
        abort_combat_recording=[](uint64_t nonce){assert(nonce==77);++aborts;};
        prepare_combat_recording=[](uint64_t,bool cold){assert(!cold);return false;};
        assert(startup->prepare_owned(77)==Capture::OwnedStart::prepare_failed && aborts==0);
        prepare_combat_recording=[](uint64_t,bool){return true;};
        assert(startup->prepare_owned(77)==Capture::OwnedStart::attach_failed && aborts==1);
        preparation_fixture=shared;
        prepare_combat_recording=[](uint64_t nonce,bool cold){assert(cold);return observation::prepare_owned(*preparation_fixture,nonce);};
        assert(startup->prepare_owned(77,true)==Capture::OwnedStart::attached && aborts==1);
        assert(startup->prepare_owned(77)==Capture::OwnedStart::unavailable);
        startup.reset();assert(!research::owned_transport_attachment.ready(77));
        prepare_combat_recording=nullptr;abort_combat_recording=nullptr;preparation_fixture=nullptr;
        // Fixture reset only: production mappings remain single-use.
        shared->reserved=0;shared->nonce=0;shared->status=0;
        shared->pid=GetCurrentProcessId();shared->nonce=77;shared->frequency=10000000;shared->status=1;
        assert(!observation::prepare_owned(*shared,88) && shared->nonce==77);
        shared->nonce=0;shared->status=0;
        assert(!observation::prepare_owned(*shared,0) && shared->status==0);
        shared->count=1;assert(!observation::prepare_owned(*shared,77));shared->count=0;
        assert(observation::prepare_owned(*shared,77) && shared->status==1 && shared->reserved==observation::owned_mode);
        assert(!observation::prepare_owned(*shared,88) && shared->nonce==77);
        auto capture=std::make_unique<Capture>();
        shared->reserved=1;assert(!capture->attach_owned(77));
        shared->reserved=observation::owned_mode;assert(!capture->attach_owned(78));
        shared->count=1;assert(!capture->attach_owned(77));shared->count=0;
        assert(!research::owned_transport_attachment.ready(77));
        assert(capture->attach_owned(77));assert(!capture->attach_owned(77));
        assert(research::owned_transport_attachment.ready(77) && !research::owned_transport_attachment.ready(78));
        research::owned_transport_attachment.detach(78);assert(research::owned_transport_attachment.ready(77));
        auto duplicate=std::make_unique<Capture>();assert(!duplicate->attach_owned(77));duplicate.reset();
        assert(research::owned_transport_attachment.ready(77));
        bool rejected=false;try{capture->arm({},77);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
        shared->records[0].data[0]=123;shared->rounds[0]={2,4};MemoryBarrier();InterlockedExchange(&shared->count,1);
        capture->update(77);assert(capture->exchange.published==1 && capture->exchange.local[0].data[0]==123);
        assert(capture->exchange.local_rounds[0].number==2);
        capture.reset();assert(!research::owned_transport_attachment.ready(77));UnmapViewOfFile(shared);CloseHandle(handle);
    }
    static Exchange a,b;
    a.published=b.published=limit;
    for(unsigned i=0;i<limit;++i)for(unsigned w=0;w<13;++w){a.local[i].data[w]=i*100+w;b.local[i].data[w]=50000+i*100+w;}
    for(unsigned i=0;i<limit;++i)a.local_rounds[i]=b.local_rounds[i]={1,42};
    for(unsigned retry=0;retry<5;++retry)for(unsigned i=0;i<limit;++i){
        Packet p;p.kind=1;p.index=(limit-1)-i;p.record=a.local[p.index];p.own_hash=1;p.received_hash=42;
        if((i+retry)%3)assert(b.accept(p));
        p.record=b.local[p.index];if((i+retry)%4)assert(a.accept(p));
    }
    assert(a.received==limit && b.received==limit);
    assert(a.remote[0].data==b.local[0].data && b.remote[(limit-1)].data==a.local[(limit-1)].data);
    Packet summary;summary.kind=2;summary.own_hash=Exchange::checksum(b.local,b.local_rounds);summary.received_hash=Exchange::checksum(b.remote,b.remote_rounds);
    assert(a.accept(summary) && a.peer_match);
    std::swap(summary.own_hash,summary.received_hash);assert(b.accept(summary) && b.peer_match);
    Packet bad;bad.kind=1;bad.index=limit;assert(!a.accept(bad));bad.index=0;bad.record=a.remote[0];bad.record.data[0]++;
    bool caught=false;try{a.accept(bad);}catch(const std::runtime_error&){caught=true;}assert(caught);
    bad.record=a.remote[0];bad.record.data[13]=1;assert(!a.accept(bad));
    summary.own_hash++;caught=false;try{b.accept(summary);}catch(const std::runtime_error&){caught=true;}assert(caught);
    bad.record=a.remote[0];bad.own_hash=1;bad.received_hash=43;
    caught=false;try{a.accept(bad);}catch(const std::runtime_error&){caught=true;}assert(caught);
    assert(Exchange::checksum(a.remote,a.remote_rounds)!=Exchange::checksum(a.remote,{}));
    sc6::steam_handshake::Protocol old_mode{2,10},new_mode{1,20};new_mode.expected_signature=0x000353433648534bULL;
    assert(!new_mode.receive(1,old_mode.outgoing(),32));
    std::cout<<"PASS: limit distinct records each direction, loss/reorder/duplicates, crossed checksums, conflict/bounds/reserved rejection, mode isolation\n";
}
