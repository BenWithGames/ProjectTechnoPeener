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
int main(){
    auto block=VirtualAlloc(nullptr,0x4900000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check(block!=nullptr,"allocation failed");memoryBase=reinterpret_cast<uintptr_t>(block);
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

