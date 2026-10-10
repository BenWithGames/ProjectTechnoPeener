#include "../src/FrameRecorder.cpp"
#include <iostream>
#include <thread>
#include <cstdlib>
void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
static uintptr_t memoryBase;
__declspec(noinline) double target(uintptr_t a,double b,uintptr_t c,double d,uintptr_t e,double f) {
    ++*reinterpret_cast<uint32_t*>(memoryBase+0x470D0C4);
    return static_cast<double>(a+c+e)+b+d+f;
}
static void* roundObject{};static int roundArgument{};
static void roundOriginal(void* object,int round){roundObject=object;roundArgument=round;SetLastError(321);}
static void* upstreamContext{};static void* upstreamEvent{};static unsigned upstreamCalls{};
static void upstreamOriginal(void* context,void* event){upstreamContext=context;upstreamEvent=event;++upstreamCalls;SetLastError(789);}
int main(){
    auto block=VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check(block!=nullptr,"allocation failed");memoryBase=reinterpret_cast<uintptr_t>(block);
    using RouteProbe=sc6::research::NativeNetworkCheckpointProbe;
    for(unsigned side=0;side<2;++side){const auto player=memoryBase+0x10000+side*0x1000;
        *reinterpret_cast<uintptr_t*>(memoryBase+0x470DE90+side*8)=player;
        *reinterpret_cast<uint8_t*>(player+0x23C)=static_cast<uint8_t>(side);
        *reinterpret_cast<uint32_t*>(player+0x324)=1;
    }
    *reinterpret_cast<uint32_t*>(memoryBase+0x4855700)=1024;
    RouteProbe::sample_input_routing(memoryBase,0);
    check(sc6::network_checkpoint_status.routing_valid[0]==1 && sc6::network_checkpoint_status.routing_values[0][0]==1024 &&
          sc6::network_checkpoint_status.dispatch_stage[0]==1,"missing dispatcher preserves valid routing samples");
    const auto routeState=memoryBase+0x13000,routeVtable=memoryBase+0x14000;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x4843ED0)=routeState;
    *reinterpret_cast<uintptr_t*>(routeState)=routeVtable;
    *reinterpret_cast<uintptr_t*>(routeVtable+0x10)=memoryBase+0x15000;
    RouteProbe::sample_input_routing(memoryBase,1);
    check(sc6::network_checkpoint_status.dispatch_stage[1]==5 && sc6::network_checkpoint_status.routing_values[1][15]==0x15000,
          "dispatcher uses verified RIP-relative address");
    *reinterpret_cast<uintptr_t*>(routeVtable+0x10)=1;
    RouteProbe::sample_input_routing(memoryBase,1);
    check(sc6::network_checkpoint_status.routing_valid[1]==1 && sc6::network_checkpoint_status.dispatch_stage[1]==3 &&
          sc6::network_checkpoint_status.routing_values[1][15]==0,"invalid handler preserves routing and clears stale diagnostics");
    auto gateProbe=std::make_unique<RouteProbe>(true);
    *reinterpret_cast<uintptr_t*>(routeState)=memoryBase+0x3E87378;
    gateProbe->stable=29;gateProbe->capture_requested=true;
    check(!gateProbe->combat_gate(memoryBase) && !gateProbe->failed && gateProbe->stable==0 && !gateProbe->capture_requested,
          "startup waits without capture or failure and resets neutral settling");
    *reinterpret_cast<uintptr_t*>(routeState)=memoryBase+0x3E873D0;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x3E873D0+0x10)=memoryBase+0x384EE0;
    check(gateProbe->combat_gate(memoryBase) && sc6::network_checkpoint_status.battle_gate==3,"verified normal battle permits existing readiness checks");
    *reinterpret_cast<uintptr_t*>(memoryBase+0x3E873D0+0x10)=memoryBase+0x384040;
    check(!RouteProbe::normal_battle(memoryBase) && sc6::network_checkpoint_status.battle_gate==2,"normal vtable with wrong handler rejects");
    gateProbe->captured=1;
    *reinterpret_cast<uintptr_t*>(routeState)=memoryBase+0x3E87378;
    check(!gateProbe->combat_gate(memoryBase) && gateProbe->failed && sc6::network_checkpoint_status.failure==23 &&
          !gateProbe->readiness->probe->restore_invoked,"state transition after first capture aborts before restoration");
    check(!RouteProbe::normal_battle(0),"invalid state lookup rejects");
    std::memset(block,0,0x4900000);
    sc6::round_event::module=memoryBase;sc6::round_event::identity.begin(1);
    *reinterpret_cast<uint32_t*>(memoryBase+0x470D0C4)=123;
    SetLastError(456);sc6::round_event::observe(2,memoryBase+0x3C467E);
    auto roundSnapshot=sc6::round_event::snapshot();
    check(roundSnapshot.round==2 && roundSnapshot.counter==123 && roundSnapshot.generation==1,"round event fields");
    check(GetLastError()==456,"round observer LastError preservation");
    sc6::round_event::observe(9,memoryBase+0x3C467F);
    check(sc6::round_event::snapshot().round==2,"unexpected round caller ignored");
    sc6::round_event::original=&roundOriginal;sc6::round_event::bridge(block,7);
    check(roundObject==block && roundArgument==7 && GetLastError()==321,"round bridge original arguments and result");
    sc6::round_event::observe(0,memoryBase+0x3C467E);
    check(!sc6::round_event::snapshot().round,"invalid round fails closed");
    auto rd=sc6::round_event::diagnostics();
    check(rd.count==4 && rd.accepted==1 && rd.foreign==2 && rd.failed,"round diagnostic rejection evidence");
    for(unsigned i=0;i<300;++i)sc6::round_event::observe(1,memoryBase+1);
    rd=sc6::round_event::diagnostics();check(rd.count==256 && rd.dropped==48,"bounded round event diagnostics");
    unsigned char eventCode=6;SetLastError(456);
    sc6::round_event::observe_upstream(&eventCode,memoryBase+123);
    check(GetLastError()==456,"upstream LastError");
    check(sc6::round_event::upstream_events[0].code==6 && sc6::round_event::upstream_events[0].counter==123,"upstream event fields");
    sc6::round_event::observe_upstream(nullptr,memoryBase+123);
    check(!sc6::round_event::upstream_events[1].readable,"upstream invalid pointer");
    sc6::round_event::upstream_original=&upstreamOriginal;
    sc6::round_event::upstream_bridge(block,&eventCode);
    check(upstreamContext==block && upstreamEvent==&eventCode && upstreamCalls==1 && GetLastError()==789,"upstream original arguments/once/LastError");
    for(unsigned i=0;i<300;++i)sc6::round_event::observe_upstream(&eventCode,memoryBase+123);
    check(sc6::round_event::upstream_count==256 && sc6::round_event::upstream_dropped==47,"upstream bounded storage");
    check(!sc6::round_event::snapshot().round,"upstream never establishes round identity");
    check(sc6::round_event::gate_count==1 && sc6::round_event::gates[0].completed && sc6::round_event::gates[0].code==6 && !sc6::round_event::active_gate,"gate completion and TLS cleanup");
    sc6::round_event::GateTrace trace{};
    sc6::round_event::active_gate=&trace;
    sc6::round_event::record_lookup(memoryBase+1,block);
    sc6::round_event::record_accessor(memoryBase+1,99);
    check(!trace.lookups && !trace.round_called,"gate ignores foreign callers");
    sc6::round_event::record_lookup(memoryBase+0x3C45DB,nullptr);
    sc6::round_event::record_lookup(memoryBase+0x3C4602,block);
    sc6::round_event::record_accessor(memoryBase+0x3C466A,-1);
    check(trace.lookups==2 && trace.nonnull==1 && trace.round_called && trace.round==-1,"gate records null lookup and invalid round exactly");
    sc6::round_event::active_gate=nullptr;

    uint16_t rawRound{};bool rawReadable{};
    *reinterpret_cast<uint16_t*>(memoryBase+0x48463A0)=2;
    sc6::round_event::sample_raw_round(memoryBase,rawRound,rawReadable);
    check(rawReadable && rawRound==2,"raw round exact 16-bit read");
    *reinterpret_cast<uint16_t*>(memoryBase+0x48463A0)=65535;
    sc6::round_event::sample_raw_round(memoryBase,rawRound,rawReadable);
    check(rawReadable && rawRound==65535,"raw round retains sentinel without interpretation");
    sc6::round_event::sample_raw_round(0,rawRound,rawReadable);
    check(!rawReadable && rawRound==0,"raw round failed read clears stale data");
    check(!sc6::round_event::snapshot().round,"raw round does not release readiness");
    auto diagnosticSnapshot=std::make_unique<sc6::research::NativeBoundarySnapshot>(sc6::research::NativeBoundarySnapshot::Mode::direction_observation);
    check(!diagnosticSnapshot->capture(memoryBase,0,nullptr,+[](uintptr_t){return false;}) &&
          diagnosticSnapshot->capture_stage==1 && !diagnosticSnapshot->mode_values_valid,"capture diagnostic identifies preflight rejection");
    diagnosticSnapshot=std::make_unique<sc6::research::NativeBoundarySnapshot>(sc6::research::NativeBoundarySnapshot::Mode::direction_observation);
    *reinterpret_cast<uint16_t*>(memoryBase+0x4717840)=0;
    check(!diagnosticSnapshot->capture(memoryBase,0,nullptr,+[](uintptr_t){return true;}) &&
          diagnosticSnapshot->capture_stage==2 && diagnosticSnapshot->mode_values_valid &&
          diagnosticSnapshot->mode_values[0]==0 && !diagnosticSnapshot->data->saved.sealed,
          "direction rejection captures actual inputs without invoking serializer");
    using Checkpoint=sc6::research::NativeNetworkCheckpointProbe;
    std::array<std::byte,40> actualMovement{},predictedMovement{};
    const float auxiliary=0.125f;std::memcpy(actualMovement.data()+24,&auxiliary,4);
    check(Checkpoint::make_movement_prediction(actualMovement,predictedMovement),"neutral network envelope allows observed movement fixture");
    for(unsigned offset=0;offset<40;offset+=4){unsigned word{};std::memcpy(&word,predictedMovement.data()+offset,4);
        if(offset==0 || offset==4 || offset==16)check(word==0x400,"P1 rightward press has exact observed words");
        else check(!std::memcmp(predictedMovement.data()+offset,actualMovement.data()+offset,4),"movement fixture preserves all other words");}
    for(unsigned offset=0;offset<24;++offset){auto invalid=actualMovement;invalid[offset]=std::byte{1};
        check(!Checkpoint::make_movement_prediction(invalid,predictedMovement),"nonneutral player or edge input rejects movement fixture");}
    unsigned nanWord=0x7fc00000;std::memcpy(actualMovement.data()+24,&nanWord,4);
    check(!Checkpoint::make_movement_prediction(actualMovement,predictedMovement),"nonfinite auxiliary rejects movement fixture");
    auto checkpoint=std::make_unique<Checkpoint>();
    Checkpoint::Key checkpointKey{55,66,1,1,GetCurrentThreadId(),100,1,80};
    sc6::NetworkGameHandoff::Boundary checkpointBoundary{1,100,1,80,{}};
    sc6::NetworkGameHandoff::Request checkpointRequest{55,66,1,100,1,80,{}};
    check(Checkpoint::matches(checkpointKey,checkpointRequest,checkpointBoundary,GetCurrentThreadId()),"checkpoint exact ownership match");
    auto changed=checkpointKey;changed.peer=67;
    check(!Checkpoint::matches(changed,checkpointRequest,checkpointBoundary,GetCurrentThreadId()),"checkpoint peer isolation");
    changed=checkpointKey;changed.generation=2;
    check(!Checkpoint::matches(changed,checkpointRequest,checkpointBoundary,GetCurrentThreadId()),"checkpoint generation isolation");
    check(!Checkpoint::matches(checkpointKey,checkpointRequest,checkpointBoundary,GetCurrentThreadId()+1),"checkpoint owner thread isolation");
    checkpoint->captured=1;checkpoint->keys[0]=checkpointKey;checkpoint->snapshots[0]->valid=true;
    Checkpoint::validated(checkpoint.get(),checkpointRequest,checkpointBoundary);
    Checkpoint::validated(checkpoint.get(),checkpointRequest,checkpointBoundary);
    check(sc6::network_checkpoint_status.matched==1 && checkpoint->linked[0],"checkpoint linked exactly once");
    checkpointRequest.input[0]=0x7f;
    Checkpoint::validated(checkpoint.get(),checkpointRequest,checkpointBoundary);
    check(checkpoint->authenticated_inputs[0][0]==std::byte{} && !checkpoint->failed,"linked network input owns bytes and ignores later queue reuse");
    checkpointRequest.input[0]=0;
    check(!checkpoint->snapshots[0]->data->restore_invoked,"checkpoint linkage never loads");
    checkpoint->prepare_linked();
    check(!checkpoint->readiness->attempted,"readiness waits for both authenticated links");
    checkpoint->captured=2;checkpoint->linked[1]=true;
    checkpoint->snapshots[0]->thread=GetCurrentThreadId();
    checkpoint->prepare_linked();
    check(checkpoint->failed && sc6::network_checkpoint_status.readiness_attempted==1 &&
          sc6::network_checkpoint_status.readiness_prepared==0,"invalid second snapshot rejects readiness");
    check(!checkpoint->readiness->probe->restore_invoked,"readiness rejection never loads state");
    checkpoint->prepare_linked();
    check(sc6::network_checkpoint_status.failure==7,"readiness failure stays latched");
    auto& sourceProbe=*checkpoint->readiness->probe;
    sourceProbe.saved.writes=2;
    sourceProbe.saved.lengths[0]=0x1840;sourceProbe.saved.traces[0].source=100;
    sourceProbe.after_save.traces[0].source=200;
    sourceProbe.rings[0].headers[0].current=100;sourceProbe.rings[2].headers[0].current=200;
    sourceProbe.saved.lengths[1]=16;sourceProbe.saved.traces[1].source=300;
    sourceProbe.after_save.traces[1].source=400;
    sourceProbe.saved.traces[1].writer=checkpoint->snapshots[0]->module+0x1234;
    checkpoint->publish_source_mismatch();
    check(sc6::network_checkpoint_status.source_mismatch_found==1 &&
          sc6::network_checkpoint_status.source_record==1 && sc6::network_checkpoint_status.source_size==16 &&
          sc6::network_checkpoint_status.source_saved==300 && sc6::network_checkpoint_status.source_next==400 &&
          sc6::network_checkpoint_status.source_writer_rva==0x1234,
          "source diagnostic skips allowed ring rotation and reports first rejected record");
    auto nextKey=checkpointKey;nextKey.frame++;
    check(Checkpoint::live_pair(checkpointKey,nextKey,nextKey,true),"live adjacent authenticated pair eligible");
    check(!Checkpoint::live_pair(checkpointKey,nextKey,nextKey,false),"unconfirmed source cannot restore");
    auto staleKey=nextKey;staleKey.frame++;
    check(!Checkpoint::live_pair(checkpointKey,nextKey,staleKey,true),"late confirmation cannot restore stale pair");
    staleKey=nextKey;staleKey.generation++;
    check(!Checkpoint::live_pair(checkpointKey,nextKey,staleKey,true),"round generation isolates restore");
    staleKey=nextKey;staleKey.peer++;
    check(!Checkpoint::live_pair(checkpointKey,nextKey,staleKey,true),"peer isolates restore");
    staleKey=nextKey;staleKey.thread++;
    check(!Checkpoint::live_pair(checkpointKey,nextKey,staleKey,true),"thread isolates restore");
    auto retryProbe=std::make_unique<Checkpoint>(true);
    retryProbe->captured=2;retryProbe->keys={checkpointKey,nextKey};
    auto* discarded=retryProbe->snapshots[0].get();
    check(!retryProbe->retry_unconfirmed(staleKey),"foreign identity cannot retry");
    retryProbe->linked[0]=true;
    check(!retryProbe->retry_unconfirmed(nextKey),"confirmed pair cannot retry");
    retryProbe->linked[0]=false;
    check(retryProbe->retry_unconfirmed(nextKey) && !retryProbe->failed &&
          retryProbe->captured==0 && retryProbe->stable==0 &&
          retryProbe->snapshots[0].get()!=discarded && !retryProbe->snapshots[0]->attempted,
          "unconfirmed pair replaced with unused preallocated snapshots");
    Checkpoint::validated(retryProbe.get(),checkpointRequest,checkpointBoundary);
    check(!retryProbe->linked[0],"late confirmation cannot link discarded pair");
    retryProbe->captured=2;retryProbe->keys={checkpointKey,nextKey};
    check(retryProbe->retry_unconfirmed(nextKey) && retryProbe->confirmation_retries==2,"second bounded retry");
    retryProbe->captured=2;retryProbe->keys={checkpointKey,nextKey};
    check(retryProbe->retry_unconfirmed(nextKey) && retryProbe->failed &&
          sc6::network_checkpoint_status.failure==15 && !retryProbe->invocation.invoked &&
          !retryProbe->readiness->probe->restore_invoked,"retry exhaustion never restores");
    sc6::network_checkpoint_status.failure=0;
    // A failed capture must never be retried by the new common dispatcher.
    auto dispatchProbe=std::make_unique<Checkpoint>(true);
    dispatchProbe->failed=true;dispatchProbe->prediction_capture_pending=true;
    sc6::network_checkpoint_status.failure=18;
    dispatchProbe->process(0,0,0,GetCurrentThreadId(),checkpointBoundary,nullptr,0);
    check(sc6::network_checkpoint_status.failure==18 && !dispatchProbe->predicted_snapshot->attempted &&
          !dispatchProbe->invocation.invoked,"failed dispatcher preserves cause and never captures or replays again");
    sc6::network_checkpoint_status.failure=0;
    auto replayProbe=std::make_unique<Checkpoint>(true);
    replayProbe->captured=2;replayProbe->keys={checkpointKey,nextKey};
    replayProbe->replay_current(0,0,0,GetCurrentThreadId(),checkpointBoundary,nullptr);
    check(replayProbe->failed && !replayProbe->readiness->probe->restore_invoked && !replayProbe->invocation.invoked,
          "invalid live gate invokes neither loader nor update");
    replayProbe->readiness->diagnostic_stage=5;
    replayProbe->readiness->probe->delayed_failure_line=552;
    replayProbe->readiness->probe->exception=0xC0000005;
    replayProbe->readiness->probe->fault=true;
    replayProbe->readiness->probe->restore_completed=true;
    replayProbe->readiness->probe->wind_restore_verified=true;
    replayProbe->readiness->probe->wind_diagnostic.failure_line=301;
    replayProbe->readiness->probe->wind_diagnostic.record=42;
    replayProbe->readiness->probe->wind_diagnostic.kind=10;
    replayProbe->readiness->probe->wind_diagnostic.size=32;
    replayProbe->readiness->probe->wind_diagnostic.vtable_rva=0x123456;
    replayProbe->publish_restore_diagnostics();
    check(sc6::network_checkpoint_status.wind_verified==1 && sc6::network_checkpoint_status.wind_capture_valid==0 &&
          sc6::network_checkpoint_status.wind_failure_line==301 && sc6::network_checkpoint_status.wind_record==42 &&
          sc6::network_checkpoint_status.wind_kind==10 && sc6::network_checkpoint_status.wind_size==32 &&
          sc6::network_checkpoint_status.wind_vtable_rva==0x123456,"wind restore and following capture reported independently");
    check(sc6::network_checkpoint_status.restore_stage==5 && sc6::network_checkpoint_status.restore_failure_line==552 &&
          sc6::network_checkpoint_status.restore_exception==0xC0000005 && sc6::network_checkpoint_status.restore_flags==3,
          "restore diagnostics preserve failure stage, line, exception and independent completed flags");
    auto typeProbe=std::make_unique<Checkpoint>();
    typeProbe->snapshots[0]->module=0x100000;
    auto& typeRecords=typeProbe->snapshots[0]->data->transforms[0];
    typeRecords.count=1;typeRecords.records[0].side=2;typeRecords.records[0].kind=4;typeRecords.records[0].size=8;
    uintptr_t unknownType=0x200000;
    std::memcpy(typeRecords.records[0].bytes.data(),&unknownType,sizeof(unknownType));
    check(!typeProbe->preflight_wind() && sc6::network_checkpoint_status.failure==14 &&
          sc6::network_checkpoint_status.wind_vtable_rva==0x100000 && !typeProbe->readiness->probe->restore_invoked,
          "unknown captured type rejected before loading without dereferencing live object");
    auto windPreflight=std::make_unique<Checkpoint>();
    auto& windSaved=windPreflight->snapshots[0]->data->transforms[0];
    auto& windNext=windPreflight->snapshots[1]->data->transforms[0];
    windSaved.count=windNext.count=1;
    for(auto* transform:{&windSaved,&windNext}){transform->records[0].side=2;transform->records[0].kind=24;transform->records[0].size=0x1C8;}
    const unsigned nonfiniteWind=0x7fc01234;
    std::memcpy(windNext.records[0].bytes.data()+0x40-0x18,&nonfiniteWind,4);
    check(!windPreflight->preflight_wind() && sc6::network_checkpoint_status.failure==22 &&
          sc6::network_checkpoint_status.wind_offset==0x40 && sc6::network_checkpoint_status.wind_next_word==nonfiniteWind &&
          !windPreflight->readiness->probe->restore_invoked,"wind preflight reports exact rejected bits before any loading");
    windNext.records[0].bytes.fill(std::byte{});
    for(unsigned off:{0x60u,0x6Cu,0x148u})std::memcpy(windNext.records[0].bytes.data()+off-0x18,&nonfiniteWind,4);
    sc6::research::TransformSnapshot::WindDiagnostic windDiagnostic;
    check(sc6::research::TransformSnapshot::stage_wind_finite(windSaved.records[0],windNext.records[0],&windDiagnostic),
          "integer wind fields retain existing exemptions");
    auto& wa=windSaved.records[0];auto& wb=windNext.records[0];
    wa.bytes.fill(std::byte{});wb.bytes.fill(std::byte{});
    auto putWind=[](auto& r,unsigned off,unsigned bits){std::memcpy(r.bytes.data()+off-0x18,&bits,4);};
    putWind(wa,0x138,0xFFFFFFFFu);putWind(wb,0x138,0xFFFFFFFFu);
    check(sc6::research::TransformSnapshot::stage_wind_finite(wa,wb),"unchanged inactive phase pattern allowed");
    putWind(wb,0xEC,0x3F800000u);
    check(!sc6::research::TransformSnapshot::stage_wind_finite(wa,wb),"active phase rejects nonfinite pattern");
    putWind(wb,0xEC,0);putWind(wb,0x138,0x7FC00000u);
    check(!sc6::research::TransformSnapshot::stage_wind_finite(wa,wb),"different inactive NaN rejected");
    putWind(wb,0x138,0);
    check(!sc6::research::TransformSnapshot::stage_wind_finite(wa,wb),"changing inactive phase rejected");
    putWind(wb,0x138,0xFFFFFFFFu);putWind(wa,0x40,0xFFFFFFFFu);
    check(!sc6::research::TransformSnapshot::stage_wind_finite(wa,wb),"other nonfinite fields remain rejected");
    auto cameraTest=std::make_unique<sc6::research::CameraSnapshot>();
    sc6::research::CameraSnapshot::RestoreDiagnostic cameraDiagnostic;
    check(!cameraTest->restore_smoothing(*cameraTest,*cameraTest,0,false,false,true,false,&cameraDiagnostic) && cameraDiagnostic.failure_line!=0,
          "camera invalid snapshot rejects and records source line without writes");
    sc6::network_checkpoint_status.replay_attempted=0;
    sc6::network_checkpoint_status.matched=0;
    sc6::round_event::GateTrace pathTrace{};
    sc6::round_event::sample_round_path(nullptr,pathTrace);
    check(pathTrace.path_status==0,"round path null context");
    auto controller=memoryBase+0x300000,pathState=memoryBase+0x301000,stateTable=memoryBase+0x302000;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x98)=controller;
    *reinterpret_cast<uintptr_t*>(controller+0x1450)=pathState;
    *reinterpret_cast<uintptr_t*>(pathState)=stateTable;
    *reinterpret_cast<uintptr_t*>(stateTable+0x100)=memoryBase+0x1234;
    sc6::round_event::sample_round_path(block,pathTrace);
    check(pathTrace.path_status==4 && pathTrace.round_getter==0x1234 && pathTrace.state==pathState,"round path exact pointer chain");
    *reinterpret_cast<uintptr_t*>(stateTable+0x100)=1;
    pathTrace={};sc6::round_event::sample_round_path(block,pathTrace);
    check(pathTrace.path_status==3 && !pathTrace.round_getter,"round path foreign getter rejected");
    pathTrace={};sc6::round_event::sample_round_path(reinterpret_cast<void*>(1),pathTrace);
    check(pathTrace.path_status==5,"round path invalid pointer contained");
    *reinterpret_cast<uintptr_t*>(memoryBase+0x98)=0;
    sc6::round_event::identity.begin(1);
    *reinterpret_cast<uint32_t*>(memoryBase+0x470D0C4)=0;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470F1A0)=memoryBase+0x10000;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470F1A8)=memoryBase+0x110000;
    // Auxiliary sampling is read-only, distinguishes unsupported/absent objects,
    // and reports invalid pointers without escaping an exception.
    auto manager=memoryBase+0x200000,owner=memoryBase+0x201000;
    auto object=memoryBase+0x202000,vt=memoryBase+0x203000;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470DF28)=manager;
    *reinterpret_cast<uintptr_t*>(manager+0xA0)=owner;
    *reinterpret_cast<uintptr_t*>(owner+8)=object;
    *reinterpret_cast<uintptr_t*>(owner+0x10+4*8)=object;
    *reinterpret_cast<uintptr_t*>(object)=vt;
    *reinterpret_cast<uintptr_t*>(vt+0x38)=memoryBase+0x31F4E0;
    *reinterpret_cast<uintptr_t*>(vt+0x28)=memoryBase+0x32ACE0;
    *reinterpret_cast<uintptr_t*>(object+0x3C8)=12345;
    *reinterpret_cast<uint32_t*>(object+0x3D0)=1;
    auto aux=sample_auxiliary(memoryBase);
    check(aux.valid && aux.entries[4].status==1 && aux.entries[4].data==12345 && aux.entries[4].flag==1 && aux.entries[0].status==0,"auxiliary fields");
    check(aux.entries[4].progress_status==2,"null auxiliary progress target");
    auto progressTarget=memoryBase+0x204000;
    *reinterpret_cast<uintptr_t*>(object+0x38)=progressTarget;
    *reinterpret_cast<uintptr_t*>(object+0x2D0)=12345;
    *reinterpret_cast<uint32_t*>(object+0x2DC)=7;
    *reinterpret_cast<uint32_t*>(object+0x2E0)=80;
    *reinterpret_cast<float*>(progressTarget+0x1DC)=80.0f;
    *reinterpret_cast<float*>(progressTarget+0x1E0)=7.0f;
    *reinterpret_cast<float*>(object+0x28)=21.5f;
    *reinterpret_cast<uint32_t*>(object+0x30)=1;
    *reinterpret_cast<uint32_t*>(object+0x388)=1;
    *reinterpret_cast<uint32_t*>(object+0x3A0)=2;
    for(size_t j=0;j<std::size(camera_outputs);++j)
        *reinterpret_cast<uint32_t*>(progressTarget+camera_outputs[j])=static_cast<uint32_t>(0x3F800000+j);
    aux=sample_auxiliary(memoryBase);
    check(aux.entries[4].camera_position_bits==0x41AC0000 && aux.entries[4].camera_enabled==1 &&
          aux.entries[4].camera_path==1 && aux.entries[4].camera_state==2,"camera position and gates");
    for(size_t j=0;j<std::size(camera_outputs);++j)
        check(aux.entries[4].camera_output_bits[j]==0x3F800000+j,"camera output bits");
    check(aux.valid && aux.entries[4].progress_status==1 && aux.entries[4].cursor==7 &&
          aux.entries[4].length==80 && aux.entries[4].sequence==12345 &&
          aux.entries[4].target_progress_bits==0x40E00000 && aux.entries[4].target_length_bits==0x42A00000,"progress snapshot");
    check(*reinterpret_cast<uint32_t*>(object+0x2DC)==7,"sampling mutated cursor");
    *reinterpret_cast<uintptr_t*>(object+0x38)=1;
    aux=sample_auxiliary(memoryBase);
    check(!aux.valid && aux.entries[4].progress_status==3,"unreadable progress target");
    *reinterpret_cast<uintptr_t*>(object+0x38)=progressTarget;
    *reinterpret_cast<uintptr_t*>(vt+0x28)=0;
    check(sample_auxiliary(memoryBase).entries[4].status==2,"unknown auxiliary reader");
    *reinterpret_cast<uintptr_t*>(owner+0x10+4*8)=1;
    aux=sample_auxiliary(memoryBase);
    check(!aux.valid && aux.entries[4].status==3,"invalid auxiliary object");
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470DF28)=0;
    auto& r=state();r.base=memoryBase;r.deadline=GetTickCount64()+20000;
    check(MH_Initialize()==MH_OK,"initialize");
    check(MH_CreateHook(reinterpret_cast<void*>(&target),reinterpret_cast<void*>(&SC6EntryBridge),&SC6Original)==MH_OK,"create");
    check(MH_EnableHook(reinterpret_cast<void*>(&target))==MH_OK,"enable");
    r.active=true;
    for(unsigned i=0;i<610;++i)check(target(17,1.25,23,2.5,31,4.75)==79.5,"argument/return corruption");
    check(r.count==600 && r.ready && !r.active && !r.fault,"bounded completion");
    check(r.skipped==0,"unexpected skips");
    for(unsigned i=0;i<600;++i)check(r.rows[i].g[0]==i && r.rows[i].argument==17,"not before original update");
    check(*reinterpret_cast<uint32_t*>(memoryBase+0x470D0C4)==610,"original call count");
    check(!r.rows[0].input_valid,"invalid envelope reported as valid");
    sc6::research::RecordedInput mapping;
    std::array<std::byte,40> expected_input{};
    for(unsigned i=0;i<40;++i)expected_input[i]=static_cast<std::byte>(i+1);
    auto map_argument=mapping.assign(expected_input);
    r.count=0;r.owner=0;r.ready=false;r.active=true;
    target(map_argument,1.25,23,2.5,31,4.75);
    check(r.count==1 && r.rows[0].input_valid && r.rows[0].input_envelope==expected_input,"input mapping envelope capture");
    mapping.bytes.fill(std::byte{});
    check(r.rows[0].input_envelope==expected_input,"mapping envelope must own its bytes");
    check(!r.full && !r.save && !r.shared && !r.native_restore,"mapping must not create restore probe");

    auto observationTest=std::make_unique<sc6::observation::Shared>();
    r.observation=observationTest.get();r.observing=true;r.observation->reserved=1;r.observation->nonce=55;r.observation->status=1;
    r.count=sc6::observation::capacity-1;r.owner=0;r.ready=false;r.fault=false;r.active=true;r.deadline=GetTickCount64()+20000;
    sc6::round_event::identity.begin(1);sc6::round_event::identity.on_round_started(1);
    sc6::round_event::event_counter=1;
    check(sc6::network_game_handoff.bind(55,66,1),"drain bind");
    target(map_argument,1.25,23,2.5,31,4.75);
    check(r.count==sc6::observation::capacity && r.active && r.observation->status==2,"long capture enters drain at capacity");
    const auto lastRecord=r.observation->records.back();
    sc6::NetworkGameHandoff::Request tail{55,66,1,lastRecord.data[0],1,1,{}};
    check(sc6::network_game_handoff.publish(tail),"publish final captured handoff");
    target(map_argument,1.25,23,2.5,31,4.75);
    check(sc6::network_game_handoff.validated==1 && sc6::network_game_handoff.read==sc6::network_game_handoff.write,"tail handoff drained after samples end");
    check(r.count==sc6::observation::capacity && r.observation->records.back().data==lastRecord.data && r.observation->count==sc6::observation::capacity,"drain does not append or overwrite samples");
    sc6::network_game_handoff.drain_finished=true;
    target(map_argument,1.25,23,2.5,31,4.75);
    check(!r.active && r.observation->status==2 && !r.full && !r.save,"drain stops without restore");
    r.observing=false;r.observation=nullptr;

    r.count=0;r.owner=0;r.ready=false;r.active=true;
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470F1A0)=1;
    check(target(17,1.25,23,2.5,31,4.75)==79.5,"fault pass-through");
    check(r.fault && r.ready && r.count==0 && !r.active,"invalid pointer must stop");
    *reinterpret_cast<uintptr_t*>(memoryBase+0x470F1A0)=memoryBase+0x10000;
    r.count=0;r.owner=GetCurrentThreadId();r.ready=false;r.fault=false;r.active=true;
    std::thread t([]{check(target(17,1.25,23,2.5,31,4.75)==79.5,"thread pass-through");});t.join();
    check(r.fault && !r.active && r.count==0,"thread change must stop");
    r.count=0;r.owner=0;r.ready=false;r.fault=false;r.active=true;
    r.charge_observe=true;r.observation_started=false;r.deadline=GetTickCount64()+45000;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && r.active && !r.full && !r.save && !r.shared,"observation wait or serializer state");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=219;
    *reinterpret_cast<uint16_t*>(memoryBase+0x47F143A)=65535;
    *reinterpret_cast<int32_t*>(memoryBase+0x4846458)=0;
    for(unsigned i=0;i<600;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==600 && !r.active && r.observation_started && !r.fault && !r.full,"observation completion");
    r.charge_observe=false;r.observation_started=false;
    r.count=0;r.owner=0;r.ready=false;r.fault=false;r.active=true;
    r.attack_observe=true;r.deadline=GetTickCount64()+45000;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && r.active && !r.observation_started,"charge incorrectly triggered attack observation");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=679;
    for(unsigned i=0;i<600;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==600 && !r.active && r.observation_started && !r.fault && !r.full && !r.save && !r.shared,"attack observation completion");
    r.attack_observe=false;r.observation_started=false;
    r.full=std::make_unique<sc6::research::FullStateProbe>();
    r.full->throw_requested=true;r.full->preserve_requested=true;
    r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=GetTickCount64()+45000;r.active=true;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && r.active && !r.full->attempted,"throw waiting consumed capture or attempted load");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=487;
    *reinterpret_cast<uint16_t*>(memoryBase+0x47F143A)=10040;
    *reinterpret_cast<int32_t*>(memoryBase+0x4846458)=-1;
    target(17,1.25,23,2.5,31,4.75);
    check(!r.full->attempted,"wrong defender move triggered");
    *reinterpret_cast<uint16_t*>(memoryBase+0x47F143A)=10041;
    target(17,1.25,23,2.5,31,4.75);
    check(r.full->trigger_matched && r.full->attempted && !r.full->restore_invoked && r.count==1 && r.fault,
        "throw trigger/preflight rejection failed");
    r.full=std::make_unique<sc6::research::FullStateProbe>();
    r.full->attack_requested=true;r.full->preserve_requested=true;
    r.restore_failed=false;r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=GetTickCount64()+45000;r.active=true;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && !r.full->attempted,"wrong move triggered attack restore");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=679;
    *reinterpret_cast<uint16_t*>(memoryBase+0x47F143A)=65535;
    *reinterpret_cast<int32_t*>(memoryBase+0x4846458)=0;
    target(17,1.25,23,2.5,31,4.75);
    check(r.full->trigger_matched && r.full->attempted && !r.full->restore_invoked && r.fault,"attack preflight must reject invalid objects");
    r.full=std::make_unique<sc6::research::FullStateProbe>();
    r.full->edge_requested=true;r.full->preserve_requested=true;
    r.restore_failed=false;r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=GetTickCount64()+45000;r.active=true;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && !r.full->attempted,"Soul Attack triggered Critical Edge restore");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=656;
    target(17,1.25,23,2.5,31,4.75);
    check(r.full->trigger_matched && r.full->attempted && !r.full->restore_invoked && r.fault,"Critical Edge preflight rejection failed");
    r.full=std::make_unique<sc6::research::FullStateProbe>();
    r.full->reversal_requested=true;r.full->preserve_requested=true;
    r.restore_failed=false;r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=GetTickCount64()+45000;r.active=true;
    for(unsigned i=0;i<610;++i)target(17,1.25,23,2.5,31,4.75);
    check(r.count==0 && !r.full->attempted,"Critical Edge triggered Reversal Edge restore");
    *reinterpret_cast<uint16_t*>(memoryBase+0x475A04A)=187;
    *reinterpret_cast<uint16_t*>(memoryBase+0x47F143A)=193;
    *reinterpret_cast<float*>(memoryBase+0x48462F4)=819.0f/16384.0f;
    *reinterpret_cast<int32_t*>(memoryBase+0x4846458)=-1;
    target(17,1.25,23,2.5,31,4.75);
    check(r.full->trigger_matched && r.full->attempted && !r.full->restore_invoked && r.fault,"Reversal Edge preflight rejection failed");
    r.full.reset();r.restore_failed=false;
    r.native_source=std::make_unique<sc6::research::NativeBoundarySnapshot>(sc6::research::NativeBoundarySnapshot::Mode::idle);
    r.native_restore=std::make_unique<sc6::research::NativeAdjacentRestore>();
    r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=GetTickCount64()+20000;r.active=true;
    target(17,1.25,23,2.5,31,4.75);
    check(r.native_source->attempted && r.native_source->fault && r.restore_failed && !r.active && r.count==1,"native source capture must fail closed");
    check(!r.native_restore->attempted && !r.native_restore->probe->restore_invoked,"source failure must not load");
    target(17,1.25,23,2.5,31,4.75);
    check(r.count==1 && !r.native_restore->attempted,"failed native experiment must not retry");
    check(!r.native_restore->run(*r.native_source,memoryBase,17) && !r.native_restore->probe->restore_invoked,"failed source must reject direct retry too");
    r.native_source.reset();r.native_restore.reset();
    r.full.reset();r.restore_failed=false;
    r.count=0;r.owner=0;r.ready=false;r.fault=false;r.deadline=0;r.active=true;
    target(17,1.25,23,2.5,31,4.75);
    check(!r.active && r.ready && r.count==0,"deadline must stop");
    check(MH_DisableHook(reinterpret_cast<void*>(&target))==MH_OK,"disable");
    check(MH_Uninitialize()==MH_OK,"uninitialize");
    VirtualFree(block,0,MEM_RELEASE);
    std::cout<<"PASS: 600 pre-update samples; 610 original calls; register/stack arguments and return; invalid pointer, thread change, timeout; throw wait beyond 600 entries, wrong move rejection, matched trigger with failed preflight; detach.\n";
}


