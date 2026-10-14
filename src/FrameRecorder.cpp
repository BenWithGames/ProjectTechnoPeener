#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <memory>
#include "FrameRecorder.hpp"
#include "SaveProbe.hpp"
#include "SharedRestoreProbe.hpp"
#include "FullStateProbe.hpp"
#include "IdleTrajectoryAudit.hpp"
#include "TwoFrameIdleReplay.hpp"
#include "NativeAdjacentRestore.hpp"
#include "NativeTwoFrameReplay.hpp"
#include "NativeImmediateReplay.hpp"
#include "NativeThreeImmediateReplay.hpp"
#include "NativePredictionProbe.hpp"
#include "NativeTwoMovement.hpp"
#include "RepeatedMovement.hpp"
#include "InputObservation.hpp"
#include "RoundEventObserver.hpp"
#include "NativeClockHook.hpp"
#include "CombatResourcePreparation.hpp"
#include "CombatPeerContext.hpp"
#include "NativeArmingDispatch.hpp"
#include "ExclusiveBattleRoute.hpp"
#include "EffectUpdateObserver.hpp"
#include "NativeRangeObserver.hpp"
#include "ResourceDescriptorObserver.hpp"
#include "ResourceLeaseService.hpp"
#include "SnapshotDependencyProbe.hpp"
#include "GeneratedOwnerObserver.hpp"
#include "NetworkGameHandoff.hpp"
#include "NetworkDepthCorrection.hpp"
#include "NativeWindowReuseProbe.hpp"

extern "C" { void* SC6Original=nullptr; void SC6EntryBridge(); }
extern "C" { void* SC6FieldOriginal=nullptr; void SC6FieldBridge(); }
extern "C" { void* SC6ResetOriginal=nullptr; void SC6ResetBridge(); }
extern "C" void SC6TraceFieldReset(const uintptr_t* registers,uintptr_t caller) noexcept {
    sc6::research::field_update_trace.record_reset(registers,caller);
}
extern "C" void SC6TraceFieldUpdate(const uintptr_t* registers,uintptr_t caller) noexcept {
    sc6::research::field_update_trace.record(registers,caller);
}
extern "C" { void* SC6AfterOriginal=nullptr; void SC6AfterBridge(); }
extern "C" void SC6TraceFieldAfter(uintptr_t player) noexcept {
    uintptr_t args[]={player,2,0,0};
    sc6::research::field_update_trace.record(args,sc6::research::field_update_trace.base+0x3793DC,2);
}
extern "C" { void* SC6BattleAfterOriginal=nullptr; void SC6BattleAfterBridge(); }
extern "C" void SC6TraceBattleAfter(uintptr_t outer) noexcept {
    auto& trace=sc6::research::field_update_trace;
    const DWORD error=GetLastError();
    if(trace.enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==trace.owner && trace.count){
        const auto last=trace.events[trace.count-1];
        if(last.assignment==2){trace.inspect_callbacks(outer);uintptr_t args[]={last.player,2,0,0};trace.record(args,trace.base+0x3FE816,3);}
    }
    SetLastError(error);
}
extern "C" { void* SC6CleanupAfterOriginal=nullptr; void SC6CleanupAfterBridge(); }
extern "C" void SC6TraceCleanupAfter(uintptr_t) noexcept {
    auto& trace=sc6::research::field_update_trace;
    const DWORD error=GetLastError();
    if(trace.enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==trace.owner && trace.count){
        const auto last=trace.events[trace.count-1];
        if(last.assignment==3){uintptr_t args[]={last.player,2,0,0};trace.record(args,trace.base+0x3fe822,4);}
    }
    SetLastError(error);
}
extern "C" { void* SC6EventsAfterOriginal=nullptr; void SC6EventsAfterBridge(); }
extern "C" void SC6TraceEventsAfter(uintptr_t) noexcept {
    auto& trace=sc6::research::field_update_trace;
    const DWORD error=GetLastError();
    if(trace.enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==trace.owner && trace.count){
        const auto last=trace.events[trace.count-1];
        if(last.assignment==4){uintptr_t args[]={last.player,2,0,0};trace.record(args,trace.base+0x3fe82a,5);}
    }
    SetLastError(error);
}
extern "C" { void* SC6TailAfterOriginal=nullptr; void SC6TailAfterBridge(); }
extern "C" void SC6TraceTailAfter(uintptr_t) noexcept {
    auto& trace=sc6::research::field_update_trace;
    const DWORD error=GetLastError();
    if(trace.enabled.load(std::memory_order_acquire) && GetCurrentThreadId()==trace.owner && trace.count){
        const auto last=trace.events[trace.count-1];
        if(last.assignment==5){uintptr_t args[]={last.player,2,0,0};trace.record(args,trace.base+0x3fe832,6);}
    }
    SetLastError(error);
}
extern "C" {void* SC6SetterZeroOriginal=nullptr;void SC6SetterZeroBridge();}
extern "C" void SC6TraceSetterZero(const uintptr_t* registers,uintptr_t site) noexcept {sc6::research::field_update_trace.record_setter(registers,site);}
extern "C" {void* SC6SetterFullOriginal=nullptr;void SC6SetterFullBridge();}
extern "C" void SC6TraceSetterFull(const uintptr_t* registers,uintptr_t site) noexcept {sc6::research::field_update_trace.record_setter(registers,site);}
namespace {
constexpr unsigned capacity=600;
// These are research fields, not a complete rollback snapshot.
struct Field { const char* name; unsigned offset; unsigned size; char type; };
constexpr Field globals[]={
 {"outer_counter",0x470D0C4,4,'u'}, {"p1_timer",0x475A038,4,'u'}, {"p2_timer",0x47F1428,4,'u'},
 {"p1_input",0x4717840,2,'u'}, {"p2_input",0x47AEC30,2,'u'},
 {"p1_move",0x475A04A,2,'u'}, {"p2_move",0x47F143A,2,'u'},
 {"gate",0x48462D0,1,'u'}, {"scale",0x48462D4,4,'f'},
 {"side0_scale",0x48462D8,4,'f'}, {"side1_scale",0x48462DC,4,'f'},
 {"side0_countdown",0x48462E0,4,'i'}, {"side1_countdown",0x48462E4,4,'i'},
 {"side0_source",0x48462E8,4,'u'}, {"side1_source",0x48462EC,4,'u'},
 {"multiplier",0x48462F4,4,'f'}, {"source",0x4846300,4,'u'},
 {"third_scale",0x4846304,4,'f'}, {"selector",0x4846458,4,'i'},
 {"side0_mode",0x484645C,4,'u'}, {"side1_mode",0x4846460,4,'u'} };
constexpr Field players[]={
 {"index",0x23C,1,'u'}, {"state",0x19EC,2,'u'}, {"base_step",0x3500,4,'f'},
 {"pending_base",0x3504,4,'f'}, {"base_countdown",0x3508,4,'i'},
 {"pending_base_duration",0x350C,4,'i'}, {"pending_scale",0x3510,4,'f'},
 {"pending_duration",0x3514,4,'i'}, {"pending_source",0x3518,4,'u'}, {"extra_flag",0x16E6,1,'u'} };
constexpr Field animation[]={ {"progress",8,4,'f'}, {"previous",12,4,'f'}, {"end",16,4,'f'}, {"rate",48,4,'f'} };
constexpr size_t ng=std::size(globals), np=std::size(players), na=std::size(animation);
// Target fields written by camera mode-3 evaluator 32E3F0; names remain offsets.
constexpr unsigned camera_outputs[]={0x1F0,0x1F4,0x1F8,0x200,0x204,0x208,0x210,0x218,0x21C,0x220,0x230,0x234,0x238,0x244,0x248,0x250,0x258};
struct AuxEntry {
    uintptr_t object{},vtable{},player{},cached{},data{};
    uintptr_t sequence{},target{};
    uint32_t cursor{},length{},target_length_bits{},target_progress_bits{};
    uint32_t camera_position_bits{},camera_enabled{},camera_path{},camera_state{};
    std::array<uint32_t,std::size(camera_outputs)> camera_output_bits{};
    unsigned progress_status{}; // 0 not sampled, 1 complete, 2 null target, 3 unreadable
    uint32_t flag{},mode{}; unsigned status{}; // 0 absent, 1 known layout, 2 unsupported, 3 unreadable
};
struct AuxFrame {
    uintptr_t manager{},owner{},buffer{};
    bool valid{};
    std::array<AuxEntry,17> entries{};
};
struct Row {
    LONGLONG qpc{}; DWORD tid{}; uintptr_t argument{},pointer[2]{};
    uint32_t g[ng]{},p[2][np]{},a[2][na]{};
    AuxFrame auxiliary;
    std::array<std::byte,40> input_envelope{};
    bool input_valid{};
    short traced_field[2]{};
};
struct Recorder {
    SRWLOCK lock=SRWLOCK_INIT;
    std::atomic<bool> active{false};
    std::atomic<unsigned> skipped{0};
    std::array<Row,capacity> rows{};
    unsigned count{}; DWORD owner{}; bool ready{},hooked{},verified{},fault{};
    uintptr_t first_return_address{}; unsigned caller_changes{};
    const char* reason="none"; uintptr_t base{}; ULONGLONG deadline{},lastPoll{};
    std::filesystem::path directory;
    std::string stem;
    std::unique_ptr<sc6::research::SaveProbe> save;
    std::unique_ptr<sc6::research::SharedRestoreProbe> shared;
    bool shared_used{},full_used{},restore_failed{};
    std::unique_ptr<sc6::research::FullStateProbe> full;
    std::unique_ptr<sc6::research::IdleTrajectoryAudit> trajectory;
    std::unique_ptr<sc6::research::TwoFrameIdleReplay> two_frame;
    std::unique_ptr<sc6::research::NativeBoundarySnapshot> native_source;
    std::unique_ptr<sc6::research::NativeAdjacentRestore> native_restore;
    bool native_replay{};
    std::unique_ptr<sc6::research::NativePredictionProbe> native_prediction;
    bool reuse_waiting{};uint64_t reuse_wait_deadline{};std::unique_ptr<sc6::research::NetworkDepthCorrection> network_checkpoints;std::unique_ptr<sc6::research::NativeWindowReuseProbe> network_reuse;
    std::unique_ptr<sc6::research::NativeTwoMovement> native_movement;
    std::unique_ptr<sc6::research::RepeatedMovement> repeated_movement;
    std::unique_ptr<sc6::research::NativeTwoFrameReplay> native_two;
    std::unique_ptr<sc6::research::NativeImmediateReplay> immediate_replay;
    std::unique_ptr<sc6::research::NativeThreeImmediateReplay> three_immediate;
    bool full_fields_equal{};
    bool charge_observe{},attack_observe{},observation_started{};
    AuxFrame auxiliary_after_probe;
    HANDLE observation_mapping{};
    sc6::observation::Shared* observation{};
    bool observing{},observation_used{},field_hooked{},reset_hooked{},field_only{},after_hooked{},battle_after_hooked{},cleanup_hooked{},events_hooked{},tail_hooked{},setter_zero_hooked{},setter_full_hooked{};
};
Recorder& state() { static auto* r=new Recorder; return *r; }
bool copy(void* dst, uintptr_t src, size_t n) noexcept {
    __try { memcpy(dst,reinterpret_cast<const void*>(src),n); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
AuxFrame sample_auxiliary(uintptr_t base) noexcept {
    AuxFrame f;
    if(!copy(&f.manager,base+0x470DF28,8) || f.manager<0x10000 ||
       !copy(&f.owner,f.manager+0xA0,8) || f.owner<0x10000 ||
       !copy(&f.buffer,f.owner+8,8))return f;
    f.valid=true;
    for(unsigned i=0;i<17;++i){auto& e=f.entries[i];
        if(!copy(&e.object,f.owner+0x10+i*8,8)){e.status=3;f.valid=false;continue;}
        if(!e.object)continue;
        uintptr_t getter{},reader{};
        if(!copy(&e.vtable,e.object,8) || !copy(&getter,e.vtable+0x38,8) ||
           !copy(&reader,e.vtable+0x28,8)){e.status=3;f.valid=false;continue;}
        if(getter!=base+0x31F4E0 || reader!=base+0x32ACE0){e.status=2;continue;}
        e.status=1;
        if(!copy(&e.player,e.object+0x3B8,8) || !copy(&e.cached,e.object+0x3C0,8) ||
           !copy(&e.data,e.object+0x3C8,8) || !copy(&e.flag,e.object+0x3D0,4) ||
           !copy(&e.mode,e.object+0xA4,4)){e.status=3;f.valid=false;continue;}
        // Mode-3 initializer 32EE30 resets these fields. Raw float bits preserve exact values.
        if(!copy(&e.sequence,e.object+0x2D0,8) || !copy(&e.cursor,e.object+0x2DC,4) ||
           !copy(&e.length,e.object+0x2E0,4) || !copy(&e.target,e.object+0x38,8) ||
           !copy(&e.camera_position_bits,e.object+0x28,4) || !copy(&e.camera_enabled,e.object+0x30,4) ||
           !copy(&e.camera_path,e.object+0x388,4) || !copy(&e.camera_state,e.object+0x3A0,4)){
            e.progress_status=3;f.valid=false;continue;
        }
        if(!e.target){e.progress_status=2;continue;}
        if(!copy(&e.target_length_bits,e.target+0x1DC,4) ||
           !copy(&e.target_progress_bits,e.target+0x1E0,4)){
            e.progress_status=3;f.valid=false;continue;
        }
        e.progress_status=1;
        for(size_t j=0;j<std::size(camera_outputs);++j)
            if(!copy(&e.camera_output_bits[j],e.target+camera_outputs[j],4)){
                e.progress_status=3;f.valid=false;break;
            }
    }
    return f;
}
template<size_t N> bool fields(uint32_t (&out)[N],uintptr_t base,const Field (&schema)[N]) noexcept {
    for(size_t i=0;i<N;++i) if(!copy(&out[i],base+schema[i].offset,schema[i].size)) return false;
    return true;
}
bool hash_matches() {
    wchar_t name[32768]{};
    if(!GetModuleFileNameW(nullptr,name,32768)) return false;
    std::ifstream f(std::filesystem::path(name),std::ios::binary);
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE h{}; unsigned char digest[32]{};
    if(!f || BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    bool ok=BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)>=0;
    char bytes[65536];
    while(ok && f) { f.read(bytes,sizeof(bytes)); auto n=f.gcount(); if(n) ok=BCryptHashData(h,reinterpret_cast<PUCHAR>(bytes),static_cast<ULONG>(n),0)>=0; }
    ok=ok && f.eof() && BCryptFinishHash(h,digest,32,0)>=0;
    if(h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg,0);
    static constexpr char hex[]="0123456789ABCDEF"; std::string actual;
    for(auto b:digest){actual+=hex[b>>4];actual+=hex[b&15];}
    return ok && actual=="F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553";
}
void status(const char* message) {
    std::ofstream(state().directory/"status.txt",std::ios::app)<<GetTickCount64()<<" "<<message<<"\n";
}
void finish(Recorder& r,const char* why,bool fault=false) noexcept {
    sc6::research::field_update_trace.stop();
    r.active.store(false,std::memory_order_release);r.ready=true;r.reason=why;r.fault=fault;
}
void value(std::ofstream& f,uint32_t v,char type) {
    if(type=='f'){float x;memcpy(&x,&v,4);f<<x;}
    else if(type=='i') f<<static_cast<int32_t>(v);else f<<v;
}
void write_capture(Recorder& r) {
    std::ofstream f(r.directory/(r.stem+".csv"));f.precision(9);
    f<<"sample,qpc,thread,entry_argument,p1_pointer,p2_pointer";
    for(auto x:globals) f<<','<<x.name;
    for(int p=0;p<2;++p){for(auto x:players)f<<",p"<<p+1<<'_'<<x.name;for(auto x:animation)f<<",p"<<p+1<<'_'<<x.name;}
    f<<'\n';
    unsigned discontinuities=0;
    for(unsigned n=0;n<r.count;++n){auto& row=r.rows[n];
        if(n && row.g[0]!=r.rows[n-1].g[0]+uint32_t(1)) ++discontinuities;
        f<<n<<','<<row.qpc<<','<<row.tid<<','<<row.argument<<','<<row.pointer[0]<<','<<row.pointer[1];
        for(size_t i=0;i<ng;++i){f<<',';value(f,row.g[i],globals[i].type);}
        for(int p=0;p<2;++p){for(size_t i=0;i<np;++i){f<<',';value(f,row.p[p][i],players[i].type);}for(size_t i=0;i<na;++i){f<<',';value(f,row.a[p][i],animation[i].type);}}
        f<<'\n';
    }
    f.flush();bool written=bool(f);
    std::ofstream input_trace(r.directory/(r.stem+"-inputs.csv"));
    input_trace<<"sample,outer,input_valid";
    for(unsigned offset=0;offset<40;offset+=4)input_trace<<",word_"<<offset;
    input_trace<<'\n';unsigned invalid_inputs=0;
    for(unsigned n=0;n<r.count;++n){const auto& row=r.rows[n];
        input_trace<<n<<','<<row.g[0]<<','<<row.input_valid;
        if(!row.input_valid)++invalid_inputs;
        for(unsigned offset=0;offset<40;offset+=4){input_trace<<',';
            if(row.input_valid){unsigned bits{};std::memcpy(&bits,row.input_envelope.data()+offset,4);input_trace<<bits;}}
        input_trace<<'\n';
    }
    input_trace.flush();const bool inputs_written=bool(input_trace);
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    std::ofstream meta(r.directory/(r.stem+".txt"));
    meta<<"schema=1\nboundary=before_RVA_0x2DBC60\nmode=user_confirmed_offline_training\ncomplete_snapshot=false\nsamples="<<r.count
        <<"\nskipped_entries="<<r.skipped.load()<<"\nstop="<<r.reason<<"\nfault="<<r.fault<<"\nouter_discontinuities="<<discontinuities<<"\nqpc_frequency="<<frequency.QuadPart<<"\ncsv_written="<<written<<'\n';
    meta<<"input_trace_written="<<inputs_written<<"\ninvalid_input_envelopes="<<invalid_inputs<<'\n';
    meta<<"entry_module_base="<<r.base<<"\nentry_return_address="<<r.first_return_address
        <<"\nentry_caller_changes="<<r.caller_changes<<'\n';
    if(r.immediate_replay){const auto& p=*r.immediate_replay;
        meta<<"scope=offline_single_synchronous_replay\nupdates_invoked="<<p.invocation.invoked
            <<"\nupdate_completed="<<p.invocation.completed<<"\nupdate_exception="<<p.invocation.exception
            <<"\nupdate_counter_before="<<p.invocation.before<<"\nupdate_counter_after="<<p.invocation.after
            <<"\nrestore_invoked="<<p.restore.probe->restore_invoked<<"\nrestore_verified="<<p.restore.verified
            <<"\nprepare_rejection="<<static_cast<unsigned>(p.restore.rejection)
            <<"\nreplay_scalars_verified="<<p.restore.replay_scalars_verified
            <<"\ncomparison_verified="<<p.restore.replay_verified<<"\nprobe_done="<<p.done
            <<"\nprobe_stage="<<p.stage<<"\nprobe_fault="<<p.fault<<'\n';}
    meta<<"attack_observation_only="<<r.attack_observe<<'\n';
    if(r.three_immediate || r.field_only){
        const auto& trace=sc6::research::field_update_trace;
        meta<<"field_trace_events="<<trace.count<<"\nfield_trace_dropped="<<trace.dropped<<'\n';
        meta<<"callbacks_attempted="<<trace.callbacks_attempted<<"\ncallbacks_valid="<<trace.callbacks_valid<<"\ncallback_total="<<trace.callback_total<<'\n';
        std::ofstream callbacks(r.directory/(r.stem+"-callbacks.csv"));
        callbacks<<"index,active,object,vtable,target,word0,word1,word2,word3,resolved_object,resolved_target\n";
        for(unsigned i=0;i<trace.callback_count;++i){const auto& c=trace.callbacks[i];
            callbacks<<c.index<<','<<c.active<<','<<c.object<<','<<c.vtable<<','<<c.target;
            for(auto word:c.words)callbacks<<','<<word;callbacks<<','<<c.resolved_object<<','<<c.resolved_target<<'\n';}
        meta<<"setter_count="<<trace.setter_count<<"\nsetter_fault="<<trace.setter_fault<<"\nsetter_overflow="<<trace.setter_overflow<<'\n';
        std::ofstream setters(r.directory/(r.stem+"-setters.csv"));
        setters<<"object,vtable,target,site,value,counter,word0,word1,word2,word3,word4,word5,word6,word7\n";
        for(unsigned i=0;i<trace.setter_count;++i){const auto& item=trace.setters[i];setters<<item.object<<','<<item.vtable<<','<<item.target<<','<<item.site<<','<<item.value<<','<<item.counter;
            for(auto word:item.words)setters<<','<<word;setters<<'\n';}
        std::ofstream events(r.directory/(r.stem+"-field-updates.csv"));
        events<<"phase,thread,counter,player,caller,index,requested_value,before,flags,readable,assignment\n";
        for(unsigned n=0;n<trace.count;++n){const auto& e=trace.events[n];
            events<<e.phase<<','<<e.thread<<','<<e.counter<<','<<e.player<<','<<e.caller<<','<<e.index<<','<<e.delta<<','<<e.before<<','<<e.flags<<','<<e.readable<<','<<e.assignment<<'\n';}
    }
    if(r.field_only){
        meta<<"scope=offline_field_trace_only\nrestore_invoked=0\nupdates_invoked=0\n";
        std::ofstream fields(r.directory/(r.stem+"-field-boundaries.csv"));fields<<"sample,counter,p1_value,p2_value\n";
        for(unsigned n=0;n<r.count;++n)fields<<n<<','<<r.rows[n].g[0]<<','<<r.rows[n].traced_field[0]<<','<<r.rows[n].traced_field[1]<<'\n';
    }
    if(r.three_immediate){const auto& p=*r.three_immediate;
        meta<<"replay_depth="<<p.steps<<"\nreplay_ticks="<<p.replay_ticks<<"\nreplay_clock_frequency="<<p.clock_frequency<<'\n';
        meta<<"training_resets_applied="<<p.resets_applied<<"\nwaited_for_increment="<<p.waited<<'\n';
        meta<<(p.steps==6?"scope=offline_six_synchronous_replay\ncaptured=":"scope=offline_three_synchronous_replay\ncaptured=")<<p.captured<<"\nprepared="<<p.prepared
            <<"\nrestored="<<p.restored<<"\nupdates_invoked="<<p.invoked<<"\ncompared="<<p.compared
            <<"\nprobe_stage="<<p.stage<<"\nprobe_hop="<<p.hop_index
            <<"\nprobe_done="<<p.done<<"\nprobe_fault="<<p.fault<<'\n';
        for(unsigned i=0;i<p.steps;++i){const auto& call=p.calls[i];const auto& hop=*p.hops[i];
            meta<<"hop"<<i<<"_invoked="<<call.invoked<<"\nhop"<<i<<"_counter_before="<<call.before
                <<"\nhop"<<i<<"_counter_after="<<call.after<<"\nhop"<<i<<"_exception="<<call.exception
                <<"\nhop"<<i<<"_rejection="<<static_cast<unsigned>(hop.rejection)
                <<"\nhop"<<i<<"_restore_invoked="<<hop.probe->restore_invoked
                <<"\nhop"<<i<<"_restore_verified="<<hop.verified
                <<"\nhop"<<i<<"_comparison="<<hop.replay_verified<<'\n';
            const auto& q=*hop.probe;
            meta<<"hop"<<i<<"_restore_delayed_failure_line="<<q.delayed_failure_line<<'\n';
            meta<<"hop"<<i<<"_restore_exception="<<q.exception<<'\n';
            meta<<"hop"<<i<<"_restore_restore_completed="<<q.restore_completed<<'\n';
            meta<<"hop"<<i<<"_restore_selected_equal="<<q.selected_equal<<'\n';
            meta<<"hop"<<i<<"_restore_postflight_ok="<<q.postflight_ok<<'\n';
            meta<<"hop"<<i<<"_restore_preserve_verified="<<q.preserve_verified<<'\n';
            meta<<"hop"<<i<<"_restore_outer_correction_verified="<<q.outer_correction_verified<<'\n';
            meta<<"hop"<<i<<"_restore_delayed_loaded="<<q.delayed_loaded<<'\n';
            meta<<"hop"<<i<<"_restore_blend_weights_verified="<<q.blend_weights_verified<<'\n';
            meta<<"hop"<<i<<"_restore_blend_blocks_verified="<<q.blend_blocks_verified<<'\n';
            meta<<"hop"<<i<<"_restore_wind_restore_verified="<<q.wind_restore_verified<<'\n';
            meta<<"hop"<<i<<"_restore_sway_restore_verified="<<q.sway_restore_verified<<'\n';
            meta<<"hop"<<i<<"_restore_constraint_restore_verified="<<q.constraint_restore_verified<<'\n';
            meta<<"hop"<<i<<"_restore_restored_valid="<<q.restored_valid<<'\n';
            meta<<"hop"<<i<<"_restore_restored_save_unchanged="<<q.restored_save_unchanged<<'\n';
            meta<<"hop"<<i<<"_restore_restored_ring_equal="<<q.restored_ring_equal<<'\n';
            meta<<"hop"<<i<<"_restore_camera_restore_verified="<<q.camera_restore_verified<<'\n';

            meta<<"hop"<<i<<"_comparison_started="<<hop.replay_checked
                <<"\nhop"<<i<<"_measurement_read="<<hop.replay_measurement_read
                <<"\nhop"<<i<<"_rng_equal="<<hop.replay_rng_equal
                <<"\nhop"<<i<<"_players_equal="<<hop.replay_players_equal
                <<"\nhop"<<i<<"_p1_scalar_byte_differences="<<hop.replay_scalar_differences[0]
                <<"\nhop"<<i<<"_p2_scalar_byte_differences="<<hop.replay_scalar_differences[1]
                <<"\nhop"<<i<<"_replay_saved="<<q.replay_done
                <<"\nhop"<<i<<"_save_fault="<<q.fault<<'\n';
            if(q.replay_done){
                meta<<"hop"<<i<<"_keys_equal="<<q.replay_keys_equal
                    <<"\nhop"<<i<<"_rings_equal="<<q.replay_ring_equal
                    <<"\nhop"<<i<<"_native_equal="<<sc6::research::TwoFrameIdleReplay::native_equal(q,r.base)
                    <<"\nhop"<<i<<"_native_raw_byte_differences="<<q.replay_byte_differences
                    <<"\nhop"<<i<<"_blend_equal="<<(q.blends[1].valid && q.blends[4].valid && q.blends[1].bytes==q.blends[4].bytes && !std::memcmp(q.blends[1].owners,q.blends[4].owners,sizeof(q.blends[1].owners)))
                    <<"\nhop"<<i<<"_transforms_equal="<<sc6::research::TwoFrameIdleReplay::transforms_equal(q.transforms[1],q.transforms[4])
                    <<"\nhop"<<i<<"_cameras_equal="<<sc6::research::TwoFrameIdleReplay::cameras_equal(q.cameras[1],q.cameras[3])<<'\n';
                for(unsigned which=0;which<2;++which){const auto& saved=which?q.replay:q.after_save;
                    std::ofstream dump(r.directory/(r.stem+"-sync-hop"+std::to_string(i)+(which?"-actual.bin":"-expected.bin")),std::ios::binary);
                    dump.write(reinterpret_cast<const char*>(saved.stream.payload.data()),static_cast<std::streamsize>(saved.stream.write_position));
                    std::ofstream layout(r.directory/(r.stem+"-sync-hop"+std::to_string(i)+(which?"-actual-layout.csv":"-expected-layout.csv")));
                    layout<<"record,offset,length,writer,source\n";std::uint64_t offset=0;
                    for(unsigned record=0;record<saved.writes;++record){
                        layout<<record<<','<<offset<<','<<saved.lengths[record]<<','<<saved.traces[record].writer<<','<<saved.traces[record].source<<'\n';
                        offset+=saved.lengths[record];}}
            }
        }}
    meta<<"charge_observation_only="<<r.charge_observe<<"\nobservation_trigger_matched="<<r.observation_started<<'\n';
    status(written?"capture saved":"capture write failed");
    if(r.charge_observe || r.attack_observe || (r.full && r.full->combat_requested())){
            std::ofstream aux(r.directory/(r.stem+"-auxiliary.csv"));
            aux<<"sample,phase,outer,valid,manager,owner,buffer,index,status,object,vtable,player,cached,data,flag,mode,progress_status,sequence,cursor,length,target,target_length_bits,target_progress_bits,camera_position_bits,camera_enabled,camera_path,camera_state";
            for(auto offset:camera_outputs)aux<<",camera_output_"<<std::hex<<offset<<std::dec<<"_bits";
            aux<<'\n';
            auto emit=[&](unsigned sample,const char* phase,uint32_t outer,const AuxFrame& frame){
                for(unsigned i=0;i<17;++i){auto& e=frame.entries[i];
                    aux<<sample<<','<<phase<<','<<outer<<','<<frame.valid<<','<<frame.manager<<','
                        <<frame.owner<<','<<frame.buffer<<','<<i<<','<<e.status<<','<<e.object<<','
                        <<e.vtable<<','<<e.player<<','<<e.cached<<','<<e.data<<','<<e.flag<<','<<e.mode<<','<<e.progress_status<<','<<e.sequence<<','
                        <<e.cursor<<','<<e.length<<','<<e.target<<','<<e.target_length_bits<<','<<e.target_progress_bits<<','<<e.camera_position_bits<<','<<e.camera_enabled<<','<<e.camera_path<<','<<e.camera_state;
                    for(auto bits:e.camera_output_bits)aux<<','<<bits;
                    aux<<'\n';
                }
            };
            for(unsigned i=0;i<r.count;++i){emit(i,"before_update",r.rows[i].g[0],r.rows[i].auxiliary);
                if(i==0 && r.full && r.full->attempted)emit(0,"after_probe",r.rows[0].g[0],r.auxiliary_after_probe);}
    }
    if(r.trajectory){
        auto& a=*r.trajectory;
        std::ofstream report(r.directory/(r.stem+"-trajectory.txt"));
        report<<"schema=1\nscope="<<(a.reversal_mode?"four_reversal_reference_boundaries":a.edge_mode?"four_edge_reference_boundaries":a.attack_mode?"four_attack_reference_boundaries":a.charge_mode?(a.target_frames==2?"two_charge_reference_boundaries":a.target_frames==3?"three_charge_reference_boundaries":"four_charge_boundaries_no_restore"):a.throw_mode?(a.target_frames==4?"four_throw_boundaries_no_restore":"three_throw_boundaries_no_restore"):"three_idle_boundaries_no_restore")<<"\nvisual_snapshot_phase=after_save_before_update\ncomplete_snapshot=false\nrestore_invoked=0\nframes="<<a.count
              <<"\ntarget_frames="<<a.target_frames<<"\ndone="<<a.done<<"\nfault="<<a.fault<<"\nexception="<<a.exception
              <<"\nreference_start_delay_updates="<<a.reference_start_delay()
              <<"\nattack_start_seen="<<a.attack_start_seen<<"\nattack_start_outer="<<a.attack_start_outer
              <<"\nstructure_observation_only="<<a.structure_only
              <<"\nauxiliary_variation_observation="<<a.auxiliary_observation()
              <<"\ndone_means_reference_collection_only=1\n";
        auto bytes=[&](const std::string& name,const void* data,size_t size){
            std::ofstream file(r.directory/(r.stem+name),std::ios::binary);
            file.write(reinterpret_cast<const char*>(data),static_cast<std::streamsize>(size));file.flush();return bool(file);
        };
        std::ofstream trace(r.directory/(r.stem+"-trajectory-transfers.csv"));
        trace<<"frame,index,offset,length,source,writer_rva\n";
        for(unsigned i=0;i<a.frames.size();++i){auto& frame=*a.frames[i];auto prefix="frame_"+std::to_string(i);
            report<<prefix<<"_valid="<<frame.valid<<'\n'<<prefix<<"_save_unchanged="<<frame.save_unchanged<<'\n'
                  <<prefix<<"_adjacent="<<frame.adjacent<<'\n'<<prefix<<"_inputs_equal="<<frame.inputs_equal<<'\n'
                  <<prefix<<"_shape_equal="<<frame.shape_equal<<'\n';
            report<<prefix<<"_input_attempted="<<frame.input_attempted<<'\n'<<prefix<<"_input_valid="<<frame.input_valid<<'\n'
                  <<prefix<<"_input_changed_mask="<<frame.input_changed_mask<<'\n'
                  <<prefix<<"_button_changed_mask="<<(frame.input_changed_mask&0xFFFFull)<<'\n'
                  <<prefix<<"_auxiliary_changed_mask="<<(frame.input_changed_mask>>16)<<'\n';
            auto suffix="-frame-"+std::to_string(i);
            if(frame.input_valid){
                report<<prefix<<"_attempted_inputs_written="<<bytes(suffix+"-attempted-inputs.bin",frame.inputs.data(),frame.inputs.size())<<'\n';
                for(unsigned section=0;section<3;++section)report<<prefix<<"_attempted_input_source_"<<section<<'='<<frame.input_sources[section]<<'\n';
            }
            if(!frame.valid)continue;
            bool files_ok=bytes(suffix+"-native.bin",frame.saved.stream.payload.data(),static_cast<size_t>(frame.saved.stream.write_position));
            files_ok=bytes(suffix+"-rings.bin",frame.rings.bytes.data(),frame.rings.bytes.size()) && files_ok;
            files_ok=bytes(suffix+"-inputs.bin",frame.inputs.data(),frame.inputs.size()) && files_ok;
            files_ok=bytes(suffix+"-scalars.bin",frame.before.fields.data(),sizeof(frame.before.fields)) && files_ok;
            for(unsigned k=0;k<frame.keys.size();++k)report<<prefix<<"_key_"<<k<<'='<<frame.keys[k]<<'\n';
            report<<prefix<<"_rng="<<frame.before.rng<<'\n';
            for(unsigned side=0;side<2;++side)report<<prefix<<"_player_"<<side<<'='<<frame.before.players[side]<<'\n';
            for(unsigned section=0;section<3;++section)report<<prefix<<"_input_source_"<<section<<'='<<frame.input_sources[section]<<'\n';
            for(unsigned j=0;j<frame.rings.headers.size();++j){auto& h=frame.rings.headers[j];
                report<<prefix<<"_ring_"<<j<<"_index="<<h.index<<'\n';
                for(unsigned slot=0;slot<3;++slot)report<<prefix<<"_ring_"<<j<<"_slot_"<<slot<<'='<<h.slots[slot]<<'\n';
            }
            std::uint64_t offset=0;
            for(unsigned j=0;j<frame.saved.writes;++j){trace<<i<<','<<j<<','<<offset<<','<<frame.saved.lengths[j]<<','
                <<frame.saved.traces[j].source<<','<<frame.saved.traces[j].writer-r.base<<'\n';offset+=frame.saved.lengths[j];}
            auto& blend=a.visuals->blends[i];auto& transform=a.visuals->transforms[i];auto& camera=a.visuals->cameras[i];
            report<<prefix<<"_blend_valid="<<blend.valid<<'\n'<<prefix<<"_transforms_valid="<<transform.valid<<'\n'
                  <<prefix<<"_transform_failure="<<transform.failure<<'\n'<<prefix<<"_camera_valid="<<camera.valid<<'\n'
                  <<prefix<<"_camera_failure="<<camera.failure<<'\n';
            if(blend.valid){
                files_ok=bytes(suffix+"-blend.bin",blend.bytes.data(),sizeof(blend.bytes)) && files_ok;
                for(unsigned side=0;side<2;++side)report<<prefix<<"_blend_owner_"<<side<<'='<<blend.owners[side]<<'\n';
            }
            if(transform.valid){
                std::ofstream tm(r.directory/(r.stem+suffix+"-transforms.csv"));
                std::ofstream td(r.directory/(r.stem+suffix+"-transforms.bin"),std::ios::binary);
                tm<<"record,side,kind,list,parent,address,offset,length\n";
                unsigned offset_bytes=0;
                for(unsigned j=0;j<transform.count;++j){auto& rec=transform.records[j];
                    tm<<j<<','<<rec.side<<','<<rec.kind<<','<<rec.list<<','<<rec.parent<<','<<rec.address<<','<<offset_bytes<<','<<rec.size<<'\n';
                    td.write(reinterpret_cast<const char*>(rec.bytes.data()),rec.size);offset_bytes+=rec.size;
                }
                tm.flush();td.flush();files_ok=bool(tm)&&bool(td)&&files_ok;
                report<<prefix<<"_transform_records="<<transform.count<<'\n';
            }
            if(camera.valid){
                std::ofstream cm(r.directory/(r.stem+suffix+"-camera.csv"));
                std::ofstream cd(r.directory/(r.stem+suffix+"-camera.bin"),std::ios::binary);
                cm<<"record,owner,offset,length,smoothing_valid\n";
                for(unsigned j=0;j<camera.count;++j){auto& rec=camera.records[j];
                    cm<<j<<','<<rec.owner<<','<<j*rec.bytes.size()<<','<<rec.bytes.size()<<','<<rec.smoothing_valid<<'\n';
                    cd.write(reinterpret_cast<const char*>(rec.bytes.data()),rec.bytes.size());
                    if(rec.smoothing_valid){auto ext=suffix+"-camera-"+std::to_string(j);
                        files_ok=bytes(ext+"-smoothing.bin",rec.smoothing.data(),rec.smoothing.size()) && files_ok;
                        files_ok=bytes(ext+"-targets.bin",rec.targets.data(),rec.targets.size()) && files_ok;
                        files_ok=bytes(ext+"-response.bin",rec.response.data(),rec.response.size()) && files_ok;
                    }
                }
                cm.flush();cd.flush();files_ok=bool(cm)&&bool(cd)&&files_ok;
                report<<prefix<<"_camera_records="<<camera.count<<'\n';
            }
            report<<prefix<<"_files_written="<<files_ok<<'\n';
        }
        for(unsigned pair=0;pair<a.combat_checks.size();++pair){if(!a.combat_checks[pair])continue;auto& q=*a.combat_checks[pair];
            report<<"pair_"<<pair<<"_preflight_attempted="<<q.combat_preflight_attempted<<'\n'
                  <<"pair_"<<pair<<"_preflight_pass_mask="<<q.combat_preflight_pass_mask<<'\n'
                  <<"pair_"<<pair<<"_preflight_required_mask="<<q.combat_preflight_required_mask<<'\n'
                  <<"pair_"<<pair<<"_root_wind_ready="<<a.root_wind_ready[pair]<<'\n'
                  <<"pair_"<<pair<<"_restore_invoked="<<q.restore_invoked<<'\n';
        }
        if(a.charge_mode)for(unsigned pair=0;pair<3;++pair)
            report<<"pair_"<<pair<<"_charge_camera_attempted="<<a.charge_camera_attempted[pair]<<'\n'
                  <<"pair_"<<pair<<"_charge_camera_ready="<<a.charge_camera_ready[pair]<<'\n';
        trace.flush();report<<"trace_written="<<bool(trace)<<'\n';
    }
    auto write_full=[&](sc6::research::FullStateProbe& s,const char* capture_scope=nullptr){
        std::ofstream report(r.directory/(r.stem+"-restore.txt"));
        report<<"schema=1\nscope="<<(capture_scope?capture_scope:(s.combat_replay_requested?(s.reversal_requested?"one_frame_reversal_restore_experiment":s.edge_requested?"one_frame_edge_restore_experiment":s.attack_requested?"one_frame_attack_restore_experiment":s.charge_requested?"one_frame_charge_restore_experiment":"one_frame_throw_restore_experiment"):s.delayed_requested?"one_frame_idle_restore_experiment":s.throw_audit?"adjacent_throw_saves_no_restore":s.delay_audit?"adjacent_idle_saves_no_restore":"native_full_serializer_immediate_roundtrip"))<<"\ncomplete_snapshot=false\nattempted="<<s.attempted
            <<"\npreflight_ok="<<s.preflight_ok<<"\nsave_valid="<<s.save_valid<<"\nrestore_invoked="<<s.restore_invoked
            <<"\nrestore_completed="<<s.restore_completed<<"\nselected_keys_equal="<<s.selected_equal
            <<"\nrecorder_fields_equal="<<r.full_fields_equal<<"\npostflight_ok="<<s.postflight_ok<<"\npost_save_valid="<<s.post_save_valid
            <<"\nfault="<<s.fault<<"\nexception="<<s.exception<<"\nphase="<<s.phase
            <<"\nwrite_transfers="<<s.saved.writes<<"\nread_transfers="<<s.saved.reads
            <<"\nbytes_written="<<s.saved.stream.write_position<<"\nbytes_read="<<s.saved.stream.read_position
            <<"\ntransfer_failures="<<s.saved.stream.failures<<"\nfirst_error="<<s.saved.stream.first_error
            <<"\npost_save_bytes="<<s.after_save.stream.write_position<<"\npost_save_transfers="<<s.after_save.writes
            <<"\npost_save_failures="<<s.after_save.stream.failures<<"\nraw_byte_differences="<<s.byte_differences
            <<"\nraw_byte_comparison_includes_padding=true\nelapsed_qpc="<<s.elapsed_qpc<<"\nqpc_frequency="<<frequency.QuadPart<<'\n';
        for(unsigned i=0;i<std::size(s.key_rvas);++i)report<<"key_"<<s.key_rvas[i]<<"_before="<<s.before_keys[i]<<"\nkey_"<<s.key_rvas[i]<<"_after="<<s.after_keys[i]<<'\n';
        report<<"audit_button_bytes_changed_mask="<<(s.input_checkpoints[1].changed_mask&0xFFFFull)
              <<"\naudit_auxiliary_bytes_changed_mask="<<(s.input_checkpoints[1].changed_mask>>16)<<'\n';
        report<<"combat_later_window="<<s.combat_later_window<<"\ncombat_first_trigger_outer="<<s.combat_first_trigger_outer<<'\n';
        report<<"combat_replay_requested="<<s.combat_replay_requested<<"\ncombat_postload_ready="<<s.combat_postload_ready
              <<"\ncombat_comparison_attempted="<<s.combat_comparison_attempted<<"\ncombat_root_wind_restored="<<s.combat_root_wind_restored<<"\ncombat_groups_restored="<<s.combat_groups_restored<<'\n';
        report<<"combat_preflight_attempted="<<s.combat_preflight_attempted<<"\ncombat_preflight_pass_mask="<<s.combat_preflight_pass_mask<<"\ncombat_preflight_required_mask="<<s.combat_preflight_required_mask<<'\n';
        report<<"throw_camera_flag_requested="<<s.combat_flag_requested<<"\nthrow_camera_flag_attempted="<<s.throw_camera_flag.attempted<<"\nthrow_camera_flag_verified="<<s.throw_camera_flag.verified<<"\nthrow_camera_flag_unchanged_verified="<<s.throw_camera_flag.unchanged_verified<<"\nthrow_camera_flag_address="<<s.throw_camera_flag.address<<'\n';
        report<<"combat_next_attempted="<<s.combat_next_attempted<<"\ncombat_next_valid="<<s.combat_next_valid<<'\n';
        report<<"combat_load_diagnostic="<<s.combat_load_diagnostic<<'\n';
        report<<"delay_audit="<<s.delay_audit<<"\naudit_done="<<s.audit_done<<"\naudit_stable="<<s.audit_stable<<"\naudit_source_changes="<<s.audit_source_changes<<"\naudit_shape_changes="<<s.audit_shape_changes<<'\n';
        report<<"reversal_requested="<<s.reversal_requested<<'\n';
        report<<"edge_requested="<<s.edge_requested<<'\n';
        report<<"attack_requested="<<s.attack_requested<<'\n';
        report<<"charge_requested="<<s.charge_requested<<"\ncombat_trigger_matched="<<s.trigger_matched<<'\n';
        report<<"throw_requested="<<s.throw_requested<<"\nthrow_trigger_matched="<<(s.throw_requested && s.trigger_matched)<<'\n';
        report<<"preservation_requested="<<s.preserve_requested<<"\npreservation_applied="<<s.preserve_applied<<"\npreservation_verified="<<s.preserve_verified<<'\n';
        report<<"measurement_schema=2\nmeasurement_player_bytes=224\n";
        for(unsigned i=0;i<s.measurements.size();++i){
            auto& m=s.measurements[i];auto name=s.measurement_names[i];
            report<<"measurement_"<<name<<"_valid="<<m.valid<<'\n';
            if(!m.valid)continue;
            report<<"rng_"<<name<<'='<<m.rng<<'\n';
            for(unsigned j=0;j<2;++j)report<<"measurement_"<<name<<"_p"<<j+1<<"_pointer="<<m.players[j]<<'\n';
            std::ofstream bytes(r.directory/(r.stem+"-fields-"+name+".bin"),std::ios::binary);
            for(auto& fields:m.fields)bytes.write(reinterpret_cast<const char*>(fields.data()),static_cast<std::streamsize>(fields.size()));
            bytes.flush();report<<"measurement_"<<name<<"_written="<<bool(bytes)<<'\n';
        }
        if(capture_scope || s.delayed_requested || s.throw_audit || s.combat_load_diagnostic){
            report<<"camera_restore_requested="<<s.camera_restore_requested<<"\ncamera_restore_verified="<<s.camera_restore_verified<<'\n';
            for(unsigned phase=0;phase<s.cameras.size();++phase){auto& camera=s.cameras[phase];
                report<<"camera_"<<phase<<"_valid="<<camera.valid<<"\ncamera_"<<phase<<"_failure="<<camera.failure
                    <<"\ncamera_"<<phase<<"_exception="<<camera.exception<<"\ncamera_"<<phase<<"_records="<<camera.count<<'\n';
                if(!camera.count)continue;
                auto prefix=r.stem+"-camera-"+std::to_string(phase);
                std::ofstream camera_meta(r.directory/(prefix+".csv"));std::ofstream data(r.directory/(prefix+".bin"),std::ios::binary);
                camera_meta<<"record,owner,offset,length\n";
                for(unsigned i=0;i<camera.count;++i){auto& c=camera.records[i];
                    camera_meta<<i<<','<<c.owner<<','<<i*c.bytes.size()<<','<<c.bytes.size()<<'\n';
                    data.write(reinterpret_cast<const char*>(c.bytes.data()),c.bytes.size());
                    report<<"camera_"<<phase<<"_record_"<<i<<"_smoothing_valid="<<c.smoothing_valid<<'\n';
                    if(c.smoothing_valid){
                        std::ofstream smoothing(r.directory/(prefix+"-smoothing-"+std::to_string(i)+".bin"),std::ios::binary);
                        smoothing.write(reinterpret_cast<const char*>(c.smoothing.data()),c.smoothing.size());
                        smoothing.flush();report<<"camera_"<<phase<<"_record_"<<i<<"_smoothing_written="<<bool(smoothing)<<'\n';
                        std::ofstream targets(r.directory/(prefix+"-targets-"+std::to_string(i)+".bin"),std::ios::binary);
                        targets.write(reinterpret_cast<const char*>(c.targets.data()),c.targets.size());
                        targets.flush();report<<"camera_"<<phase<<"_record_"<<i<<"_targets_written="<<bool(targets)<<'\n';
                        std::ofstream response(r.directory/(prefix+"-response-"+std::to_string(i)+".bin"),std::ios::binary);
                        response.write(reinterpret_cast<const char*>(c.response.data()),c.response.size());
                        response.flush();report<<"camera_"<<phase<<"_record_"<<i<<"_response_written="<<bool(response)<<'\n';
                    }
                }
                camera_meta.flush();data.flush();report<<"camera_"<<phase<<"_written="<<(bool(camera_meta)&&bool(data))<<'\n';
            }
            report<<"constraint_restore_verified="<<s.constraint_restore_verified<<'\n';
            report<<"sway_restore_verified="<<s.sway_restore_verified<<'\n';
            report<<"wind_restore_verified="<<s.wind_restore_verified<<'\n';
            report<<"blend_weights_applied="<<s.blend_weights_applied<<"\nblend_weights_verified="<<s.blend_weights_verified<<'\n';
            report<<"blend_blocks_applied="<<s.blend_blocks_applied<<"\nblend_blocks_verified="<<s.blend_blocks_verified<<'\n';
            for(unsigned i=0;i<s.blends.size();++i){
                auto& blend=s.blends[i];auto name=s.blend_names[i];
                report<<"blend_"<<name<<"_valid="<<blend.valid<<'\n';
                if(blend.valid){
                std::ofstream bytes(r.directory/(r.stem+"-blend-"+name+".bin"),std::ios::binary);
                for(unsigned side=0;side<2;++side){
                    report<<"blend_"<<name<<"_owner"<<side<<'='<<blend.owners[side]<<'\n';
                    bytes.write(reinterpret_cast<const char*>(blend.bytes[side].data()),static_cast<std::streamsize>(blend.bytes[side].size()));
                }
                bytes.flush();report<<"blend_"<<name<<"_written="<<bool(bytes)<<'\n';
                }
                auto& t=s.transforms[i];report<<"transforms_"<<name<<"_valid="<<t.valid<<'\n';
                report<<"transforms_"<<name<<"_failure="<<t.failure<<"\ntransforms_"<<name<<"_failure_address="<<t.failure_address<<"\ntransforms_"<<name<<"_exception="<<t.trapped_exception<<"\ntransforms_"<<name<<"_records="<<t.count<<'\n';
                if(t.count){
                    std::ofstream transform_meta(r.directory/(r.stem+"-transforms-"+name+".csv"));
                    std::ofstream payload(r.directory/(r.stem+"-transforms-"+name+".bin"),std::ios::binary);
                    transform_meta<<"record,side,kind,list,parent,address,offset,length\n";
                    unsigned offset=0;
                    for(unsigned j=0;j<t.count;++j){auto& rec=t.records[j];
                        transform_meta<<j<<','<<rec.side<<','<<rec.kind<<','<<rec.list<<','<<rec.parent<<','<<rec.address<<','<<offset<<','<<rec.size<<'\n';
                        payload.write(reinterpret_cast<const char*>(rec.bytes.data()),rec.size);offset+=rec.size;
                    }
                    transform_meta.flush();payload.flush();report<<"transforms_"<<name<<"_written="<<(bool(transform_meta)&&bool(payload))<<'\n';
                }
            }
            report<<"restored_valid="<<s.restored_valid<<"\nrestored_save_unchanged="<<s.restored_save_unchanged<<"\nrestored_ring_equal="<<s.restored_ring_equal<<"\nrestored_byte_differences="<<s.restored_byte_differences<<'\n';
            if(s.restored_valid){std::ofstream bytes(r.directory/(r.stem+"-restored.bin"),std::ios::binary);
                bytes.write(reinterpret_cast<const char*>(s.restored.stream.payload.data()),static_cast<std::streamsize>(s.restored.stream.write_position));}
            if(s.restored_rings.valid){std::ofstream bytes(r.directory/(r.stem+"-restored-rings.bin"),std::ios::binary);
                bytes.write(reinterpret_cast<const char*>(s.restored_rings.bytes.data()),s.restored_rings.bytes.size());}
            report<<"outer_correction_applied="<<s.outer_correction_applied<<"\nouter_correction_verified="<<s.outer_correction_verified<<'\n';
            report<<"delayed_keys_captured="<<s.delayed_keys_captured<<'\n';
            if(s.delayed_keys_captured){
                for(unsigned i=0;i<std::size(s.key_rvas);++i)report<<"loaded_key_"<<s.key_rvas[i]<<'='<<s.delayed_loaded_keys[i]<<'\n';
                for(unsigned i=0;i<2;++i)report<<"loaded_player_"<<i<<'='<<s.delayed_loaded_players[i]<<'\n';
            }
            report<<"delayed_guard_ok="<<s.delayed_guard_ok<<"\ndelayed_inputs_equal="<<s.delayed_inputs_equal<<"\ndelayed_loaded="<<s.delayed_loaded
                <<"\nreplay_done="<<s.replay_done<<"\nreplay_keys_equal="<<s.replay_keys_equal<<"\nreplay_ring_equal="<<s.replay_ring_equal<<"\nreplay_byte_differences="<<s.replay_byte_differences<<'\n';
            report<<"input_check_phase="<<s.input_check_phase<<'\n';
            std::ofstream branch_trace(r.directory/(r.stem+"-blend-branch.csv"));
            branch_trace<<"phase,side,previous_mode,current_mode,wrapper_count,player_78,player_210\n";
            for(unsigned phase=0;phase<2;++phase){unsigned measurement=phase?2u:0u;
                if(!s.measurements[measurement].valid || !s.blends[phase].valid)continue;
                for(unsigned side=0;side<2;++side){auto bytes=s.measurements[measurement].fields[side].data()+128;
                    unsigned previous{},current{},count{};
                    std::memcpy(&previous,bytes+0x34,4);std::memcpy(&current,bytes+0x38,4);std::memcpy(&count,bytes+0x58,4);
                    branch_trace<<phase<<','<<side<<','<<previous<<','<<current<<','<<count<<','
                        <<s.blend_branch_inputs[phase][side][0]<<','<<s.blend_branch_inputs[phase][side][1]<<'\n';
                }
            }
            branch_trace.flush();report<<"blend_branch_trace_written="<<bool(branch_trace)<<'\n';
            std::ofstream input_trace(r.directory/(r.stem+"-input-checkpoints.csv"));
            input_trace<<"phase,section,offset,source,baseline,observed,changed\n";
            for(unsigned phase=0;phase<s.input_checkpoints.size();++phase){auto& c=s.input_checkpoints[phase];
                report<<"input_phase_"<<phase<<"_attempted="<<c.attempted<<"\ninput_phase_"<<phase<<"_valid="<<c.valid
                    <<"\ninput_phase_"<<phase<<"_changed_mask="<<c.changed_mask<<'\n';
                if(!c.valid)continue;
                for(unsigned i=0;i<c.bytes.size();++i){auto section=i<8?0u:i<16?1u:2u;auto offset=i-(section==0?0u:section==1?8u:16u);
                    input_trace<<phase<<','<<section<<','<<offset<<','<<c.sources[section]<<','
                        <<std::to_integer<unsigned>(s.delayed_inputs[i])<<','<<std::to_integer<unsigned>(c.bytes[i])<<','
                        <<(c.bytes[i]!=s.delayed_inputs[i])<<'\n';
                }
            }
            input_trace.flush();report<<"input_checkpoints_written="<<bool(input_trace)<<'\n';
            if(s.replay.sealed){std::ofstream bytes(r.directory/(r.stem+(s.combat_load_diagnostic?"-next-frame.bin":"-replay.bin")),std::ios::binary);
                bytes.write(reinterpret_cast<const char*>(s.replay.stream.payload.data()),static_cast<std::streamsize>(s.replay.stream.write_position));}
            if(s.replay_rings.valid){std::ofstream bytes(r.directory/(r.stem+(s.combat_load_diagnostic?"-next-frame-rings.bin":"-replay-rings.bin")),std::ios::binary);
                bytes.write(reinterpret_cast<const char*>(s.replay_rings.bytes.data()),s.replay_rings.bytes.size());}
        }
        if(s.ring_capture_enabled){
            report<<"ring_save_unchanged="<<s.ring_save_unchanged<<"\nring_rotation_ok="<<s.ring_rotation_ok<<'\n';
            std::ofstream headers(r.directory/(r.stem+"-rings.csv"));
            headers<<"checkpoint,player,kind,object,vtable,index,current,previous,slot0,slot1,slot2\n";
            for(unsigned phase=0;phase<4;++phase){auto& ring=s.rings[phase];
                report<<"ring_"<<phase<<"_valid="<<ring.valid<<'\n';if(!ring.valid)continue;
                for(unsigned j=0;j<4;++j){auto& h=ring.headers[j];
                    headers<<phase<<','<<h.player<<','<<j%2<<','<<h.object<<','<<h.vtable<<','<<h.index<<','<<h.current<<','<<h.previous;
                    for(auto slot:h.slots)headers<<','<<slot;headers<<'\n';
                }
                std::ofstream payload(r.directory/(r.stem+"-rings-"+std::to_string(phase)+".bin"),std::ios::binary);
                payload.write(reinterpret_cast<const char*>(ring.bytes.data()),ring.bytes.size());payload.flush();
                report<<"ring_"<<phase<<"_written="<<bool(payload)<<'\n';
            }
            headers.flush();report<<"ring_headers_written="<<bool(headers)<<'\n';
        }
        // Export captured addresses only; never dereference them after the probe.
        std::ofstream trace(r.directory/(r.stem+"-transfers.csv"));
        trace<<"phase,index,offset,length,source,destination,writer_return,reader_return,module_base\n";
        auto emit_trace=[&](const char* phase,const sc6::research::FullRestoreStream& stream){
            std::uint64_t offset=0;
            for(unsigned i=0;i<stream.writes;++i){auto& t=stream.traces[i];
                trace<<phase<<','<<i<<','<<offset<<','<<stream.lengths[i]<<','<<t.source<<','
                    <<t.destination<<','<<t.writer<<','<<t.reader<<','<<r.base<<'\n';
                offset+=stream.lengths[i];
            }
        };
        emit_trace("before",s.saved);emit_trace("after",s.after_save);
        if(s.delayed_requested){emit_trace("restored",s.restored);emit_trace("replay",s.replay);}
        if(s.combat_next_valid)emit_trace("next_frame",s.replay);
        trace.flush();report<<"transfer_trace_written="<<bool(trace)<<'\n';
        if(s.save_valid || (capture_scope && s.saved.sealed)){std::ofstream bytes(r.directory/(r.stem+"-before.bin"),std::ios::binary);
            bytes.write(reinterpret_cast<const char*>(s.saved.stream.payload.data()),static_cast<std::streamsize>(s.saved.stream.write_position));}
        if(s.post_save_valid){std::ofstream bytes(r.directory/(r.stem+"-after.bin"),std::ios::binary);
            bytes.write(reinterpret_cast<const char*>(s.after_save.stream.payload.data()),static_cast<std::streamsize>(s.after_save.stream.write_position));}
    };
    auto write_movement=[&](sc6::research::NativeTwoMovement& n){
        const auto stem=r.stem;
        std::ofstream report(r.directory/(stem+"-two-movement.txt"));
        report<<"scope=bounded_movement_correction\nwindow="<<n.window<<"\ndirection_bits="<<n.direction_bits<<"\ncomplete_snapshot=false\ncaptured="<<n.captured
              <<"\nrestored="<<n.restored<<"\ndelivered="<<n.delivered<<"\ncompared="<<n.compared
              <<"\nstate_changed="<<n.state_changed<<"\ncorrected="<<n.corrected<<"\ndone="<<n.done<<"\nfault="<<n.fault
              <<"\nfailure_stage="<<n.failure_stage<<'\n';
        for(unsigned i=0;i<2*n.window;++i){auto& h=*n.restores[i];
            report<<"restore"<<i<<"_verified="<<h.verified<<"\nrestore"<<i<<"_rejection="<<sc6::research::NativeBoundarySnapshot::rejection_name(h.rejection)<<'\n';
            r.stem=stem+"-restore"+std::to_string(i);write_full(*h.probe);}
        for(unsigned i=0;i<n.captured;++i){r.stem=stem+"-reference"+std::to_string(i);write_full(*n.frames[i]->data,"single_boundary_snapshot_no_restore");}
        r.stem=stem;
    };
    if(r.native_movement)write_movement(*r.native_movement);
    if(r.repeated_movement){
        auto& batch=*r.repeated_movement;const auto stem=r.stem;
        std::ofstream report(r.directory/(stem+"-repeated-movement.txt"));
        report<<"scope=repeated_three_frame_correction\ncompleted="<<batch.completed<<"\ndone="<<batch.done<<"\nfault="<<batch.fault<<'\n';
        for(unsigned i=0;i<batch.cycles.size();++i)if(batch.cycles[i]){
            r.stem=stem+"-cycle"+std::to_string(i);write_movement(*batch.cycles[i]);}
        r.stem=stem;
    }
    if(r.native_prediction){
        auto& n=*r.native_prediction;const auto stem=r.stem;
        std::ofstream report(r.directory/(stem+"-native-prediction.txt"));
        report<<"schema=1\nscope="<<(n.a_restore?"standing_a_prediction_correction":n.a_capture?"standing_a_prediction_capture":n.two_frame_capture?"two_frame_screen_right_capture":n.movement_restore?"screen_right_prediction_correction":n.direction_mode?"screen_right_state_capture":"auxiliary_float_prediction_correction")<<"\ncomplete_snapshot=false\n"
              <<"captured="<<n.captured<<"\nrestored="<<n.loads<<"\ndelivered="<<n.deliveries
              <<"\ndirty_detected="<<n.dirty_detected<<"\nobserved_state_changed="<<n.state_changed
              <<"\ncorrected="<<n.corrected<<"\ndone="<<n.done<<"\nfault="<<n.fault
              <<"\nmovement_capture_only="<<(n.direction_mode && !n.movement_restore)<<"\nmovement_restore_requested="<<n.movement_restore<<"\nupdates_invoked="<<n.immediate_call.invoked<<"\nsynchronous="<<n.synchronous<<"\ncomposed_inputs="<<n.compose_inputs<<"\ncomposition_passed="<<n.composition_passed<<"\nsynthetic_peer_aux_different="<<n.peer_aux_different<<"\ntraining_reset_applied="<<n.training_reset_applied<<"\nimmediate_completed="<<n.immediate_call.completed<<"\nimmediate_exception="<<n.immediate_call.exception<<"\ndirection_mode="<<n.direction_mode<<"\nprediction_observed="<<n.prediction_observed
              <<"\nprediction_player_input="<<n.prediction_player_input<<"\nprediction_player_move="<<n.prediction_player_move
              <<"\nsettle_samples="<<n.settle.samples<<"\nsettle_stable="<<n.settle.stable<<"\nsettle_ready="<<n.settle.ready
              <<"\nfailure_stage="<<n.failure_stage<<'\n';
        for(unsigned i=0;i<2;++i){auto& h=*n.restores[i];
            report<<"restore"<<i<<"_rejection="<<sc6::research::NativeBoundarySnapshot::rejection_name(h.rejection)
                  <<"\nrestore"<<i<<"_verified="<<h.verified<<"\nrestore"<<i<<"_replay="<<h.replay_verified<<'\n';
            r.stem=stem+"-restore"+std::to_string(i);write_full(*h.probe);
        }
        for(unsigned i=0;i<n.captured;++i){r.stem=stem+"-reference"+std::to_string(i);write_full(*n.frames[i]->data,"single_boundary_snapshot_no_restore");}
        for(unsigned i=0;i<2;++i){std::ofstream input(r.directory/(stem+(i?"-actual-input.bin":"-predicted-input.bin")),std::ios::binary);
            const auto& bytes=i?n.actual:n.predicted;input.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        r.stem=stem;
    }
    if(r.native_two){
        auto& n=*r.native_two;
        std::ofstream report(r.directory/(r.stem+"-native-two.txt"));
        report<<"schema=1\nscope=two_frame_native_idle_replay\ncomplete_snapshot=false\n"
              <<"captured="<<n.captured<<"\nprepared="<<n.prepared<<"\nrestored="<<n.restored
              <<"\ndelivered="<<n.delivered<<"\ncompared="<<n.compared<<"\ndone="<<n.done<<"\nfault="<<n.fault
              <<"\nupdates_invoked=0\npredicted_inputs_injected=0\n";
        const auto stem=r.stem;
        for(unsigned i=0;i<2;++i){auto& h=*n.hops[i];
            report<<"hop"<<i<<"_rejection="<<sc6::research::NativeBoundarySnapshot::rejection_name(h.rejection)
                  <<"\nhop"<<i<<"_restored="<<h.verified<<"\nhop"<<i<<"_replay="<<h.replay_verified<<'\n';
            r.stem=stem+"-hop"+std::to_string(i);write_full(*h.probe);
        }
        for(unsigned i=0;i<n.captured;++i){r.stem=stem+"-reference"+std::to_string(i);write_full(*n.frames[i]->data,"single_boundary_snapshot_no_restore");
            std::ofstream inputs(r.directory/(r.stem+"-inputs.bin"),std::ios::binary);
            inputs.write(reinterpret_cast<const char*>(n.frames[i]->data->delayed_inputs.data()),40);
        }
        r.stem=stem;
    }
    if(r.native_restore){
        auto& a=*r.native_restore;
        std::ofstream report(r.directory/(r.stem+"-native-adjacent.txt"));
        report<<"schema=1\nscope=live_context_one_frame_idle_restore\ncomplete_snapshot=false\n"
              <<"source_valid="<<r.native_source->valid<<"\nsource_fault="<<r.native_source->fault
              <<"\nsource_exception="<<r.native_source->exception<<"\nsource_reads="<<r.native_source->data->saved.reads
              <<"\ncurrent_valid="<<a.current.valid<<"\ncurrent_fault="<<a.current.fault<<"\ncurrent_exception="<<a.current.exception
              <<"\nattempted="<<a.attempted<<"\nprepared="<<a.prepared<<"\nrestore_invoked="<<a.probe->restore_invoked
              <<"\nrestore_completed="<<a.probe->restore_completed<<"\nverified="<<a.verified<<"\nfault="<<a.fault
              <<"\nupdates_invoked=0\npredicted_inputs_injected=0\n";
        report<<"replay_scalars_verified="<<a.replay_scalars_verified<<'\n';
        report<<"replay_requested="<<r.native_replay<<"\nreplay_delivered="<<a.replay_delivered<<"\nreplay_checked="<<a.replay_checked<<"\nreplay_verified="<<a.replay_verified<<'\n';
        report<<"immediate_completion_verified="<<a.completion.verified<<"\ncompletion_patches="<<a.completion.count<<"\nnative_payload_verified="<<a.native_payload_verified<<'\n';
        report<<"restore_input_used="<<a.restore_input_used<<'\n';
        if(a.restore_input_used){std::ofstream input(r.directory/(r.stem+"-restore-inputs.bin"),std::ios::binary);
            input.write(reinterpret_cast<const char*>(a.restore_input.bytes.data()),a.restore_input.bytes.size());
            report<<"restore_inputs_written="<<bool(input)<<'\n';}
        report<<"preparation_rejection="<<sc6::research::NativeBoundarySnapshot::rejection_name(a.rejection)<<'\n';
        const auto original_stem=r.stem;
        for(unsigned side=0;side<2;++side){
            auto& boundary=side?a.current:*r.native_source;auto& d=*boundary.data;
            const char* label=side?"current":"source";
            report<<label<<"_outer="<<d.before_keys[0]<<'\n'<<label<<"_rng="<<d.measurements[0].rng<<'\n';
            report<<label<<"_thread="<<boundary.thread<<'\n'<<label<<"_module="<<boundary.module<<'\n';
            r.stem=original_stem+"-"+label;
            write_full(d,"single_boundary_snapshot_no_restore");
            std::ofstream inputs(r.directory/(r.stem+"-inputs.bin"),std::ios::binary);
            inputs.write(reinterpret_cast<const char*>(d.delayed_inputs.data()),d.delayed_inputs.size());
            report<<label<<"_inputs_written="<<bool(inputs)<<'\n';
        }
        r.stem=original_stem;
        write_full(*a.probe);
    }
    if(r.full)write_full(*r.full);
    if(r.two_frame){
        auto& two=*r.two_frame;
        std::ofstream report(r.directory/(r.stem+"-two-frame.txt"));
        report<<"schema=1\nscope="<<(two.reversal_mode?"three_update_reversal_replay":two.edge_mode?"three_update_edge_replay":two.attack_mode?"three_update_attack_replay":two.charge_mode?(two.target_updates==3?"three_update_charge_replay":two.target_updates==2?"two_update_charge_replay":"one_update_charge_replay"):two.target_updates==3?"three_update_throw_replay_via_adjacent_restores":two.combat_mode?"two_update_throw_replay_via_adjacent_restores":"two_update_idle_replay_via_adjacent_restores")<<"\ncomplete_snapshot=false\nprepared="<<two.prepared
              <<"\ninput_delivery_phase=next_natural_update\nrecorded_inputs="<<two.recorded_inputs<<"\ndelivered_frames="<<two.delivered_frames
              <<"\nrestoration_started="<<two.restoration_started<<"\ncompleted_restores="<<two.completed_restores
              <<"\nrewound="<<two.rewound<<"\ncompared_frames="<<two.compared_frames
              <<"\nframe_1_scalars_match="<<two.scalar_matches[0]<<"\nframe_2_scalars_match="<<two.scalar_matches[1]
              <<"\nframe_1_matches="<<two.matches[0]<<"\nframe_2_matches="<<two.matches[1]
              <<"\ntarget_updates="<<two.target_updates<<"\nframe_3_scalars_match="<<two.scalar_matches[2]<<"\nframe_3_matches="<<two.matches[2]
              <<"\ndone="<<two.done<<"\nfault="<<two.fault<<"\nexception="<<two.exception<<"\nphase="<<two.phase<<'\n';
        const auto original_stem=r.stem;
        for(unsigned i=0;i<two.target_updates;++i){r.stem=original_stem+"-hop-"+std::to_string(i);write_full(*two.hops[i]);}
        r.stem=original_stem;
    }
    if(r.shared) {
        auto& s=*r.shared;
        std::ofstream report(r.directory/(r.stem+"-restore.txt"));
        report<<"schema=1\nscope=shared_state_only\ncomplete_snapshot=false\nattempted="<<s.attempted
            <<"\nsave_valid="<<s.saved<<"\nrestore_invoked="<<s.restore_invoked<<"\nrestore_completed="<<s.restored
            <<"\nstate_equal="<<s.equal<<"\nfault="<<s.fault<<"\nexception="<<s.exception
            <<"\ntransfer_failures="<<s.stream.failures<<"\nfirst_error="<<s.stream.first_error
            <<"\nwrite_transfers="<<s.stream.transfers<<"\nread_transfers="<<s.reads
            <<"\nbytes_written="<<s.stream.write_position<<"\nbytes_read="<<s.stream.read_position
            <<"\nouter_before="<<s.outer_before<<"\nouter_after="<<s.outer_after
            <<"\nelapsed_qpc="<<s.elapsed_qpc<<"\nqpc_frequency="<<frequency.QuadPart<<'\n';
    }
    if(r.save) {
        auto& s=*r.save;
        bool valid=s.invoked && !s.fault && !s.stream.failures && s.stream.write_position>0;
        bool saved=false;
        if(valid) {
            std::ofstream binary(r.directory/(r.stem+".bin"),std::ios::binary);
            binary.write(reinterpret_cast<const char*>(s.stream.payload.data()),static_cast<std::streamsize>(s.stream.write_position));
            binary.flush(); saved=bool(binary);
        }
        std::ofstream report(r.directory/(r.stem+"-save.txt"));
        report<<"schema=1\ncomplete_snapshot=false\nrestore_performed=false\nattempted="<<s.attempted
            <<"\npreflight_ok="<<s.preflight_ok<<"\ninvoked="<<s.invoked<<"\nfault="<<s.fault
            <<"\ntransfer_failures="<<s.stream.failures<<"\nfirst_error="<<s.stream.first_error
            <<"\ntransfers="<<s.stream.transfers<<"\nbytes="<<s.stream.write_position
            <<"\nelapsed_qpc="<<s.elapsed_qpc<<"\nqpc_frequency="<<frequency.QuadPart
            <<"\nbookkeeping_before="<<s.bookkeeping_before<<"\nbookkeeping_after="<<s.bookkeeping_after
            <<"\npayload_written="<<saved<<'\n';
    }
}
}
extern "C" void SC6RecordEntry(uintptr_t argument,uintptr_t* replacement=nullptr,uintptr_t return_address=0,uintptr_t* continuation=nullptr) noexcept {
    struct LastErrorGuard { DWORD value=GetLastError(); ~LastErrorGuard(){SetLastError(value);} } guard;
    auto& r=state();
    // Persistent boundary service runs before the recording gate and outside r.lock.
    if(r.verified){uint32_t frame{};if(copy(&frame,r.base+0x470D0C4,4))sc6::research::lease_service::dispatch(frame);sc6::research::generated_observer::observe_frame();}
    if(auto* preparation=sc6::research::combat_resource_preparation.load(std::memory_order_acquire)){
        if(preparation->replay_restart()){
            // Dirty sessions remain stopped, but owner-thread cleanup must not
            // be starved by the guard which suppresses the normal update.
            preparation->request_stop();uint32_t cleanup_frame{};
            copy(&cleanup_frame,r.base+0x470D0C4,4);preparation->frame(cleanup_frame,false);
            if(continuation)*continuation=0;return;
        }
        uint32_t frame{};sc6::research::NativeClockDiscovery::Observation clock;
        const auto round=sc6::round_event::snapshot();
        const bool valid=return_address==r.base+0x3FE816 && round.round && round.generation &&
            copy(&frame,r.base+0x470D0C4,4) && sc6::research::native_clock_hook::discovery.read(GetTickCount64(),clock) &&
            clock.base==r.base && clock.thread==GetCurrentThreadId();
        preparation->frame(frame,valid);
        if(valid && (preparation->hold_test.state.load()==sc6::research::NativeHoldRehearsal<>::released || preparation->cycles_enabled.load(std::memory_order_acquire))){
            sc6::research::CombatPeerContext::Snapshot peer;
            if(sc6::research::combat_peer_context.read(GetTickCount64(),peer)){
                sc6::research::RollbackBoundaryKey key{peer.local_nonce,peer.remote_nonce,round.generation,
                    round.round,round.counter,frame,peer.local_slot,GetCurrentThreadId()};
                preparation->capture_boundary(key,r.base,argument);
                if(continuation)preparation->replay_boundary(key,r.base,argument,reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original));
                if(preparation->replay_restart()){if(continuation)*continuation=0;return;}
            }
        }
    }
    const auto* combat=sc6::research::combat_routes.load(std::memory_order_acquire);
    if(auto* route=combat?combat->battle:sc6::research::exclusive_battle_route.load(std::memory_order_acquire)){
        if(continuation){*continuation=0;route->run(argument,return_address);}
        return;
    }
    if(!r.active.load(std::memory_order_acquire)){
        if(TryAcquireSRWLockExclusive(&r.lock)){if(!r.active.load() && r.network_reuse && (!r.reuse_waiting || GetTickCount64()>=r.reuse_wait_deadline))r.network_reuse->stop_ownership();ReleaseSRWLockExclusive(&r.lock);}
        if(r.verified)sc6::research::dependency_probe::frame(r.base,argument);
        return;
    }
    // Never wait behind disk I/O or a reentrant call at the game boundary.
    if(!TryAcquireSRWLockExclusive(&r.lock)){r.skipped.fetch_add(1);return;}
    if(!r.active.load()){ReleaseSRWLockExclusive(&r.lock);return;}
    DWORD tid=GetCurrentThreadId();
    if(r.owner && r.owner!=tid){finish(r,"thread_changed",true);ReleaseSRWLockExclusive(&r.lock);return;}
    r.owner=tid;
    if(!r.first_return_address)r.first_return_address=return_address;
    else if(return_address!=r.first_return_address)++r.caller_changes;
    if(GetTickCount64()>=r.deadline){finish(r,"timeout");ReleaseSRWLockExclusive(&r.lock);return;}
    if(r.observing){
        // Network modes return before offline experiments. Only the separately
        // opted-in NETWORK_REPLAY_LONG mode can invoke the one-shot replay here.
        sc6::observation::Record record;
        std::array<std::byte,40> envelope{};uint32_t after{};
        const bool ok=copy(&record.data[0],r.base+0x470D0C4,4) &&
            sc6::research::FullStateProbe::input_bytes(argument,envelope) &&
            copy(&after,r.base+0x470D0C4,4) && after==record.data[0];
        if(!ok){InterlockedExchange(&r.observation->status,3);finish(r,"observation_read_failed",true);}
        else {
            std::memcpy(record.data.data()+1,envelope.data(),40);
            LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
            record.data[11]=static_cast<uint32_t>(stamp.QuadPart);record.data[12]=static_cast<uint32_t>(stamp.QuadPart>>32);
            const auto record_limit=r.observation->reserved?sc6::observation::capacity:sc6::observation::limit;
            const bool recording=r.count<record_limit;
            if(recording)r.observation->records[r.count]=record;
            const auto round=sc6::round_event::snapshot();
            if(recording)r.observation->rounds[r.count]={round.round,round.counter};
            sc6::NetworkGameHandoff::Boundary boundary{round.generation,record.data[0],round.round,round.counter,{}};
            std::memcpy(boundary.input.data(),envelope.data(),40);
            if(r.network_reuse)r.network_reuse->process(r.base,argument,return_address,tid,boundary,reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original),r.observation->nonce);
            else if(r.network_checkpoints)r.network_checkpoints->process(r.base,argument,return_address,tid,boundary,
                reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original),r.observation->nonce);
            else sc6::network_game_handoff.boundary(r.observation->nonce,tid,boundary);
            if(recording){
                MemoryBarrier();InterlockedExchange(&r.observation->count,static_cast<LONG>(++r.count));
                if(r.count==record_limit){InterlockedExchange(&r.observation->status,2);if(!r.observation->reserved)r.active.store(false);}
            }else if(sc6::network_game_handoff.drain_finished.load(std::memory_order_acquire))r.active.store(false);
            // After the last sample, keep validation-only boundaries running
            // until delivery/draining finishes or the existing deadline expires.
        }
        ReleaseSRWLockExclusive(&r.lock);return;
    }
    if(r.field_only && r.count==0)sc6::research::field_update_trace.start(r.base);
    auto& row=r.rows[r.count]; row={}; row.argument=argument;row.tid=tid;
    row.input_valid=sc6::research::FullStateProbe::input_bytes(argument,row.input_envelope);
    LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);row.qpc=qpc.QuadPart;
    bool ok=fields(row.g,r.base,globals);
    for(int p=0;p<2 && ok;++p){
        ok=copy(&row.pointer[p],r.base+0x470F1A0+p*8,8) && row.pointer[p]>=0x10000 && row.pointer[p]<0x800000000000ULL;
        if(ok && r.field_only)ok=copy(&row.traced_field[p],row.pointer[p]+0x1B3C,2);
        if(ok)ok=fields(row.p[p],row.pointer[p],players) && fields(row.a[p],r.base+(p?0x47F0FD0:0x4759BE0),animation);
    }
    uint32_t after{};
    ok=ok && copy(&after,r.base+0x470D0C4,4) && after==row.g[0];
    if(r.field_only){
        if(!ok)finish(r,"field_observation_failed",true);
        else if(++r.count==capacity)finish(r,"limit");
        ReleaseSRWLockExclusive(&r.lock);return;
    }
    if(!ok)finish(r,"invalid_or_changing_state",true);
    else {
        if(r.charge_observe || r.attack_observe){
            if(!r.observation_started){
                float multiplier{};std::memcpy(&multiplier,&row.g[15],4);
                if(!(r.attack_observe ? (row.g[5]==679 && row.g[6]==65535 && multiplier==0.0f && row.g[18]==0) : sc6::research::FullStateProbe::charge_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])))){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                r.observation_started=true;r.deadline=GetTickCount64()+20000;
            }
            row.auxiliary=sample_auxiliary(r.base);
        }
        if(r.three_immediate && !r.three_immediate->done){
            if(!r.three_immediate->step(r.base,argument,return_address,
                reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original))){
                r.restore_failed=true;++r.count;finish(r,"three_synchronous_replay_failed",true);
                ReleaseSRWLockExclusive(&r.lock);return;
            }
        }
        if(r.immediate_replay && !r.immediate_replay->done){
            if(!r.immediate_replay->step(r.base,argument,return_address,
                reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original))){
                r.restore_failed=true;++r.count;finish(r,"synchronous_replay_failed",true);
                ReleaseSRWLockExclusive(&r.lock);return;
            }
        }
        if(r.repeated_movement && !r.repeated_movement->done){
            if(!r.repeated_movement->step(r.base,argument,replacement!=nullptr)){r.restore_failed=true;++r.count;finish(r,"repeated_movement_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
            if(replacement && r.repeated_movement->next_argument)*replacement=r.repeated_movement->next_argument;
        }
        if(r.native_movement && !r.native_movement->done){
            if(!r.native_movement->step(r.base,argument,replacement!=nullptr)){r.restore_failed=true;++r.count;finish(r,"two_movement_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
            if(replacement && r.native_movement->next_argument)*replacement=r.native_movement->next_argument;
        }
        if(r.native_prediction && !r.native_prediction->done){
            const bool prediction_ok=r.native_prediction->step(r.base,argument,replacement!=nullptr,nullptr,nullptr,nullptr,reinterpret_cast<sc6::research::SingleUpdateCall::Routine>(SC6Original),return_address);
            if(!prediction_ok){r.restore_failed=true;++r.count;finish(r,"native_prediction_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
            if(replacement && r.native_prediction->next_argument)*replacement=r.native_prediction->next_argument;
        }
        if(r.native_two && !r.native_two->done){
            const bool native_two_ok=r.native_two->step(r.base,argument,replacement!=nullptr);
            if(!native_two_ok){r.restore_failed=true;++r.count;finish(r,"native_two_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
            if(replacement && r.native_two->next_argument)*replacement=r.native_two->next_argument;
        }
        if(r.native_restore && (r.native_replay?!r.native_restore->replay_checked:!r.native_restore->attempted)){
            const bool native_ok=r.native_replay?r.native_restore->replay_step(*r.native_source,r.base,argument,replacement!=nullptr):r.native_restore->step(*r.native_source,r.base,argument);
            if(native_ok && replacement && r.native_restore->next_argument)*replacement=r.native_restore->next_argument;
            if(!native_ok){r.restore_failed=true;++r.count;finish(r,"native_adjacent_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.trajectory && !r.trajectory->done){
            if((r.trajectory->throw_mode || r.trajectory->charge_mode || r.trajectory->attack_mode || r.trajectory->edge_mode || r.trajectory->reversal_mode) && !r.trajectory->count && !r.trajectory->attack_start_seen){
                float multiplier{};std::memcpy(&multiplier,&row.g[15],4);
                if(!(r.trajectory->reversal_mode?sc6::research::FullStateProbe::reversal_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->edge_mode?sc6::research::FullStateProbe::edge_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->attack_mode?sc6::research::FullStateProbe::attack_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->charge_mode?
                    (sc6::research::FullStateProbe::charge_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])) && sc6::research::FullStateProbe::charge_aux_active(r.base)):
                    sc6::research::FullStateProbe::throw_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])))){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                r.deadline=GetTickCount64()+20000;
            }
            r.trajectory->step(r.base,argument);
            if(r.trajectory->fault){r.restore_failed=true;++r.count;finish(r,"trajectory_audit_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.two_frame && r.trajectory && r.trajectory->done && !r.two_frame->done){
            if(!r.two_frame->prepared)r.two_frame->prepare(*r.trajectory);
            if(r.two_frame->recorded_inputs && !replacement)r.two_frame->fault=true;
            r.two_frame->step(r.base,argument);
            if(replacement && !r.two_frame->fault && r.two_frame->next_argument)*replacement=r.two_frame->next_argument;
            if(r.two_frame->fault){r.restore_failed=true;++r.count;finish(r,"two_frame_replay_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.save && !r.save->attempted) r.save->run(r.base);
        if(r.shared && !r.shared->attempted) {
            r.shared->run(r.base);
            if(r.shared->fault){++r.count;finish(r,"shared_restore_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.full && r.full->combat_load_diagnostic && r.full->attempted && !r.full->combat_next_attempted){
            r.full->capture_combat_next(r.base);
            if(r.full->fault){r.restore_failed=true;++r.count;finish(r,"combat_next_capture_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.full && r.full->combat_requested() && r.full->attempted)
            row.auxiliary=sample_auxiliary(r.base);
        if(r.full && r.full->delayed_requested && !r.full->replay_done){
            if(r.full->combat_replay_requested && !r.full->audit_started){
                float multiplier{};std::memcpy(&multiplier,&row.g[15],4);
                if(!r.full->accept_replay_trigger(sc6::research::FullStateProbe::throw_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])),row.g[0])){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                r.full->trigger_matched=true;r.deadline=GetTickCount64()+20000;
            }
            r.full->delayed_step(r.base,argument);
            if(r.full->fault){r.restore_failed=true;++r.count;finish(r,"delayed_restore_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.full && !r.full->delayed_requested && r.full->delay_audit && !r.full->audit_done){
            if(r.full->throw_audit && !r.full->audit_started){
                float multiplier{};std::memcpy(&multiplier,&row.g[15],4);
                if(!(r.trajectory->reversal_mode?sc6::research::FullStateProbe::reversal_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->edge_mode?sc6::research::FullStateProbe::edge_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->attack_mode?sc6::research::FullStateProbe::attack_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])):r.trajectory->charge_mode?
                    (sc6::research::FullStateProbe::charge_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])) && sc6::research::FullStateProbe::charge_aux_active(r.base)):
                    sc6::research::FullStateProbe::throw_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18])))){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                r.full->trigger_matched=true;r.deadline=GetTickCount64()+20000;
            }
            unsigned audit_phase=r.full->audit_started?1u:0u;
            if(r.full->throw_audit && (!r.full->capture_audit_inputs(argument,audit_phase) || !r.full->capture_blend(r.base,audit_phase))){
                ++r.count;finish(r,"throw_audit_snapshot_failed",true);ReleaseSRWLockExclusive(&r.lock);return;
            }
            r.full->audit_step(r.base);
            if(r.full->throw_audit && !r.full->fault)
                r.full->cameras[audit_phase].capture(audit_phase?r.full->after_save:r.full->saved,r.base);
            if(r.full->throw_audit && r.full->audit_done && !r.full->fault)r.full->combat_replay_preflight(r.base);
            if(r.full->fault){r.restore_failed=true;++r.count;finish(r,"delay_audit_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(r.full && !r.full->delay_audit && !r.full->attempted){
            if(r.full->combat_requested()){
                // Schema positions: moves 5/6, multiplier 15, selector 18.
                float multiplier{};std::memcpy(&multiplier,&row.g[15],4);
                if(!r.full->trigger_values(row.g[5],row.g[6],multiplier,static_cast<int32_t>(row.g[18]))){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                if(r.full->charge_requested && !sc6::research::FullStateProbe::charge_aux_active(r.base)){
                    ReleaseSRWLockExclusive(&r.lock);return;
                }
                r.full->trigger_matched=true;r.deadline=GetTickCount64()+20000;
            }
            if(r.full->combat_requested())row.auxiliary=sample_auxiliary(r.base);
            r.full->run(r.base);
            if(r.full->combat_requested())r.auxiliary_after_probe=sample_auxiliary(r.base);
            Row check{};
            bool same=fields(check.g,r.base,globals);
            for(int p=0;p<2 && same;++p){
                same=copy(&check.pointer[p],r.base+0x470F1A0+p*8,8) && check.pointer[p]==row.pointer[p];
                if(same)same=fields(check.p[p],check.pointer[p],players) && fields(check.a[p],r.base+(p?0x47F0FD0:0x4759BE0),animation);
            }
            r.full_fields_equal=same && !memcmp(check.g,row.g,sizeof(row.g)) && !memcmp(check.p,row.p,sizeof(row.p)) && !memcmp(check.a,row.a,sizeof(row.a));
            if(!r.full_fields_equal)r.full->fault=true;
            if(r.full->fault){r.restore_failed=true;++r.count;finish(r,"full_restore_failed",true);ReleaseSRWLockExclusive(&r.lock);return;}
        }
        if(++r.count==capacity)finish(r,"limit");
    }
    ReleaseSRWLockExclusive(&r.lock);
}
namespace sc6 {
void recorder_init() {
    auto& r=state();r.base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&SC6RecordEntry),&module)) return;
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
    r.directory=std::filesystem::path(path).parent_path().parent_path()/"captures";
    std::filesystem::create_directories(r.directory);
    r.verified=hash_matches();status(r.verified?"ready; hook inactive until offline arm":"unsupported executable; recorder disabled");
    if(r.verified){
        // Pinned pass-through entry hook supplies boundaries even without recording.
        const unsigned char expected[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x6b,0x18};
        auto target=reinterpret_cast<void*>(r.base+0x2DBC60);
        const auto initialized=MH_Initialize();
        if((initialized==MH_OK || initialized==MH_ERROR_ALREADY_INITIALIZED) && !std::memcmp(target,expected,sizeof(expected)) &&
           MH_CreateHook(target,reinterpret_cast<void*>(&SC6EntryBridge),&SC6Original)==MH_OK){
            if(MH_EnableHook(target)==MH_OK)r.hooked=true;else MH_RemoveHook(target);
        }
        status(r.hooked?"persistent boundary observer installed":"persistent boundary observer unavailable");
        status(sc6::round_event::install(r.base)?"round event observer installed":"round event unavailable; network round agreement remains unverified");
        status(sc6::research::native_clock_hook::install(r.base)?"native clock dispatcher installed; stock pass-through":"native clock dispatcher unavailable; stock networking retained");
        r.observation_mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(sc6::observation::Shared),sc6::observation::mapping_name(GetCurrentProcessId()).c_str());
        if(r.observation_mapping && GetLastError()!=ERROR_ALREADY_EXISTS){
            r.observation=static_cast<sc6::observation::Shared*>(MapViewOfFile(r.observation_mapping,FILE_MAP_WRITE,0,0,sizeof(sc6::observation::Shared)));
            if(r.observation){*r.observation={};r.observation->schema=sc6::observation::version;r.observation->pid=GetCurrentProcessId();
                LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);r.observation->frequency=frequency.QuadPart;
                MemoryBarrier();r.observation->signature=sc6::observation::magic;}
        }
    }
}
bool prepare_owned_replay_impl(){
 auto& r=state();AcquireSRWLockExclusive(&r.lock);
 if(!r.verified || r.active.load() || r.observation_used || r.network_reuse){ReleaseSRWLockExclusive(&r.lock);return false;}
 r.network_reuse=std::make_unique<sc6::research::NativeWindowReuseProbe>();
 if(!r.network_reuse->start_ownership(r.base)){r.network_reuse.reset();ReleaseSRWLockExclusive(&r.lock);return false;}
 r.reuse_waiting=true;r.reuse_wait_deadline=GetTickCount64()+600000;
 ReleaseSRWLockExclusive(&r.lock);return true;
}
bool prepare_combat_recording_impl(std::uint64_t nonce,bool first_match){
 auto& r=state();AcquireSRWLockExclusive(&r.lock);
 const bool eligible=r.verified && r.hooked && !r.active.load() && !r.observation_used && !r.network_reuse &&
     !r.observing && !r.restore_failed && r.observation && !sc6::research::combat_resource_preparation.load();
 const bool prepared=eligible && sc6::observation::prepare_owned(*r.observation,nonce);
 if(prepared){
     r.observation_used=true;
     auto resources=std::make_unique<sc6::research::CombatResourcePreparation<>>();
     if(!resources->start(r.base,first_match)){
         InterlockedExchange(&r.observation->status,3);ReleaseSRWLockExclusive(&r.lock);return false;
     }
     sc6::research::combat_resource_preparation.store(resources.release(),std::memory_order_release);
 }
 ReleaseSRWLockExclusive(&r.lock);return prepared;
}
static std::unique_ptr<sc6::research::NativeArmingDispatch> continuous_request;
static std::uint64_t continuous_request_nonce{};
unsigned continuous_start_state_impl(){return continuous_request?1u+static_cast<unsigned>(continuous_request->state()):0u;}
bool queue_continuous_start_impl(const ContinuousStartRequest& request){
 auto& r=state();AcquireSRWLockExclusive(&r.lock);bool result=false;
 try {result=[&]{
     using namespace sc6::research;
     if(continuous_request || !request.private_match_confirmed || !request.build || !request.ticket ||
        !r.verified || !r.hooked || !SC6Original || !r.observation || r.observation->nonce!=request.local_nonce ||
        r.observation->reserved!=sc6::observation::owned_mode || r.observation->count || r.observation->status!=1)return false;
     const auto now=GetTickCount64();CombatPeerContext::Snapshot peer;NativeClockDiscovery::Observation clock;
     if(!combat_peer_context.read(now,peer) || peer.peer!=request.steam_peer || peer.local_nonce!=request.local_nonce ||
        peer.remote_nonce!=request.remote_nonce || peer.local_slot!=request.local_slot ||
        !native_clock_hook::discovery.read(now,clock) || clock.base!=r.base || clock.clock.local_mask!=request.local_slot ||
        clock.clock.identity!=request.clock_identity || clock.clock.frame>=request.clock_frame || now>UINT64_MAX-10000)return false;
     auto* resources=combat_resource_preparation.load(std::memory_order_acquire);
     const auto round=round_event::snapshot();
     if(!resources || !resources->ready.load() || resources->failed.load() || resources->retired.load() ||
        resources->owner_thread.load()!=clock.thread || !round.round || !round.generation)return false;
     NativeCombatSession::Config config;config.general_restore=true;
     config.base=r.base;config.caller=r.base+0x3FE816;config.clock_object=clock.object;
     config.original=reinterpret_cast<SingleUpdateCall::Routine>(SC6Original);
     config.identity.session=request.local_nonce;config.identity.peer=request.remote_nonce;
     config.identity.generation=round.generation;config.identity.round=round.round;config.identity.event=round.counter;
     config.identity.frame=request.battle_frame;config.identity.slot=request.local_slot;config.identity.thread=clock.thread;
     config.clock_identity=request.clock_identity;config.clock_frame=request.clock_frame;
     config.offer={request.local_nonce,request.remote_nonce,request.ticket,request.build,round.round,round.counter,
         request.battle_frame,request.local_slot,0,3,observation::capacity};
     config.image_verified=config.bilateral_private=config.boundaries_owned=config.verified_void_entry=true;
     // No arbitrary character/profile approval is inferred from the handshake.
     auto queued=std::make_unique<NativeArmingDispatch>(config,ReplayDependencySource{},*r.observation,request.steam_peer,now+10000);
     if(!queued->source_on_owner(resources,[](void* p){return static_cast<CombatResourcePreparation<>*>(p)->source_on_owner();}) || !queued->queue())return false;
     continuous_request_nonce=request.local_nonce;continuous_request=std::move(queued);return true;
 }();}catch(...){result=false;}
 ReleaseSRWLockExclusive(&r.lock);return result;
}
void abort_combat_recording_impl(std::uint64_t nonce){
 auto& r=state();AcquireSRWLockExclusive(&r.lock);
 if(nonce && nonce==continuous_request_nonce && continuous_request)continuous_request->cancel();
 if(nonce && r.observation && r.observation->reserved==sc6::observation::owned_mode &&
     r.observation->nonce==nonce && r.observation->count==0){
     InterlockedCompareExchange(&r.observation->status,3,1);
     if(auto* resources=sc6::research::combat_resource_preparation.load())resources->request_stop();
 }
 ReleaseSRWLockExclusive(&r.lock);
}
void recorder_poll() {
    prepare_owned_replay=prepare_owned_replay_impl;
    prepare_combat_recording=prepare_combat_recording_impl;
    abort_combat_recording=abort_combat_recording_impl;
    queue_continuous_start=queue_continuous_start_impl;
    continuous_start_state=continuous_start_state_impl;
    auto& r=state();auto now=GetTickCount64();if(now-r.lastPoll<250)return;r.lastPoll=now;
    if(r.verified)sc6::research::effect_observer::poll(r.base,r.directory);
    if(r.verified)sc6::research::range_observer::poll(r.base,r.directory);
    if(r.verified)sc6::research::resource_observer::poll(r.base,r.directory);
    if(r.verified)sc6::research::generated_observer::poll(r.base,r.directory);
    if(r.verified)sc6::research::dependency_probe::poll(r.base,r.directory);
    static ULONGLONG next_round_status=0;
    if(r.verified && now>=next_round_status){
        next_round_status=now+1000;
        const auto marker=round_event::snapshot();const auto diagnostic=round_event::diagnostics();
        std::ofstream ready(r.directory/("round-ready-"+std::to_string(GetCurrentProcessId())+".txt"));
        ready<<"schema=1\npid="<<GetCurrentProcessId()<<"\ntick_ms="<<now
             <<"\nhook_installed="<<diagnostic.installed<<"\nevents="<<diagnostic.count
             <<"\nupstream_installed="<<diagnostic.upstream_installed<<"\nupstream_events="<<diagnostic.upstream_count
             <<"\nfailed="<<diagnostic.failed<<"\nround="<<marker.round<<"\nboundary="<<marker.counter
             <<"\ngeneration="<<marker.generation<<"\nready="<<(marker.round!=0 && marker.generation!=0)<<'\n';
    }
    if(!TryAcquireSRWLockExclusive(&r.lock))return;
    if(r.observation && r.observation->reserved==sc6::observation::owned_mode){ReleaseSRWLockExclusive(&r.lock);return;}
    if(r.observing){
        if(r.active.load() && now>=r.deadline){r.active.store(false);InterlockedExchange(&r.observation->status,3);}
        if(!r.active.load()){if(r.observation->status==1)InterlockedExchange(&r.observation->status,3);r.ready=false;}
        ReleaseSRWLockExclusive(&r.lock);return;
    }
    if(r.active.load() && now>=r.deadline)finish(r,"timeout");
    if(r.ready){write_capture(r);r.ready=false;}
    ReleaseSRWLockExclusive(&r.lock);
    if(!r.verified || r.active.load() || r.restore_failed)return;
    bool full_request=std::filesystem::exists(r.directory/"offline-full-restore.arm");
    bool shared_request=std::filesystem::exists(r.directory/"offline-shared-restore.arm");
    bool save_request=std::filesystem::exists(r.directory/"offline-save.arm");
    const bool observe_network=std::filesystem::exists(r.directory/"network-observe.arm");
    auto arm=r.directory/(observe_network?"network-observe.arm":full_request?"offline-full-restore.arm":shared_request?"offline-shared-restore.arm":save_request?"offline-save.arm":"offline-training.arm");std::ifstream request(arm);if(!request)return;
    std::string token;std::getline(request,token);request.close();std::filesystem::remove(arm);
    if(observe_network){
        std::istringstream parser(token);std::string label,extra;unsigned pid{};std::uint64_t nonce{};
        parser>>label>>pid>>nonce;
        if(!parser || (parser>>extra) || (label!="NETWORK_OBSERVE_ONLY" && label!="NETWORK_OBSERVE_LONG" && label!="NETWORK_CHECKPOINT_LONG" && label!="NETWORK_REPLAY_LONG" && label!="NETWORK_LATE_LONG" && label!="NETWORK_REUSE_LONG") || pid!=GetCurrentProcessId() || !nonce || !r.observation || r.observation_used ||
            full_request || shared_request || save_request || std::filesystem::exists(r.directory/"offline-training.arm")){
            status("network observation arm rejected; stale, conflicting, or unavailable");return;
        }
        if(!r.hooked){
            const unsigned char expected[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x6b,0x18};unsigned char actual[sizeof(expected)]{};
            void* target=reinterpret_cast<void*>(r.base+0x2DBC60);
            if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || memcmp(actual,expected,sizeof(actual))){status("observation prologue mismatch");return;}
            auto initialized=MH_Initialize();if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED){status("observation hook init failed");return;}
            if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6EntryBridge),&SC6Original)!=MH_OK){status("observation hook creation failed");return;}
            if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("observation hook enable failed");return;}r.hooked=true;
        }
        AcquireSRWLockExclusive(&r.lock);
        r.observing=true;r.observation_used=true;r.count=0;r.owner=0;r.skipped=0;r.ready=false;
        r.network_checkpoints=(label=="NETWORK_CHECKPOINT_LONG" || label=="NETWORK_REPLAY_LONG")?std::make_unique<sc6::research::NetworkDepthCorrection>(label=="NETWORK_REPLAY_LONG"):nullptr;
        if(label!="NETWORK_REUSE_LONG" && r.network_reuse){r.observing=false;ReleaseSRWLockExclusive(&r.lock);status("owned replay already prepared; restart before changing experiment");return;}
        if(label=="NETWORK_REUSE_LONG" && (!r.network_reuse || !r.reuse_waiting || r.network_reuse->ownership_stopped)){r.observing=false;ReleaseSRWLockExclusive(&r.lock);status("replay ownership not prepared in lobby; not armed");return;}r.reuse_waiting=false;
        sc6::network_game_handoff.late_enabled=label=="NETWORK_LATE_LONG";
        sc6::network_checkpoint_status.replay_enabled=label=="NETWORK_REPLAY_LONG" || label=="NETWORK_REUSE_LONG";
        sc6::network_checkpoint_status.enabled=(r.network_checkpoints || r.network_reuse)?1u:0u;
        r.observation->reserved=label!="NETWORK_OBSERVE_ONLY"?1u:0u;
        r.observation->nonce=nonce;r.observation->count=0;InterlockedExchange(&r.observation->status,1);
        r.deadline=now+(r.observation->reserved?240000:20000);r.active.store(true,std::memory_order_release);ReleaseSRWLockExclusive(&r.lock);
        status((label=="NETWORK_REPLAY_LONG" || label=="NETWORK_REUSE_LONG")?"private one-frame replay experiment armed; restart required after failure":"network checkpoint/observation armed; no restore or replay");return;
    }
    bool attack_observe_request=!full_request && !shared_request && !save_request && token=="OFFLINE_ATTACK_OBSERVE "+std::to_string(GetCurrentProcessId());
    bool observe_request=!full_request && !shared_request && !save_request && token=="OFFLINE_CHARGE_OBSERVE "+std::to_string(GetCurrentProcessId());
    bool throw_request=full_request && token=="OFFLINE_THROW_PRESERVE "+std::to_string(GetCurrentProcessId());
    bool combat_later_request=full_request && token=="OFFLINE_THROW_REPLAY_LATER "+std::to_string(GetCurrentProcessId());
    bool combat_replay_request=combat_later_request || (full_request && token=="OFFLINE_THROW_REPLAY "+std::to_string(GetCurrentProcessId()));
    bool repeated_movement_request=full_request && token=="OFFLINE_REPEATED_MOVEMENT "+std::to_string(GetCurrentProcessId());
    bool native_left_request=full_request && token=="OFFLINE_NATIVE_LEFT_MOVEMENT "+std::to_string(GetCurrentProcessId());
    bool native_three_movement_request=full_request && token=="OFFLINE_NATIVE_THREE_MOVEMENT "+std::to_string(GetCurrentProcessId());
    bool native_movement_request=native_left_request || native_three_movement_request || (full_request && token=="OFFLINE_NATIVE_TWO_MOVEMENT "+std::to_string(GetCurrentProcessId()));
    bool native_two_direction_request=full_request && token=="OFFLINE_NATIVE_TWO_DIRECTION_CAPTURE "+std::to_string(GetCurrentProcessId());
    bool native_direction_restore_request=full_request && token=="OFFLINE_NATIVE_DIRECTION_RESTORE "+std::to_string(GetCurrentProcessId());
    bool native_direction_request=native_two_direction_request || native_direction_restore_request || (full_request && token=="OFFLINE_NATIVE_DIRECTION "+std::to_string(GetCurrentProcessId()));
    bool composed_a_request=full_request && token=="OFFLINE_COMPOSED_A_CORRECTION "+std::to_string(GetCurrentProcessId());
    bool immediate_a_request=composed_a_request || (full_request && token=="OFFLINE_IMMEDIATE_A_CORRECTION "+std::to_string(GetCurrentProcessId()));
    bool native_a_restore_request=immediate_a_request || (full_request && token=="OFFLINE_NATIVE_A_RESTORE "+std::to_string(GetCurrentProcessId()));
    bool native_a_request=native_a_restore_request || (full_request && token=="OFFLINE_NATIVE_A_CAPTURE "+std::to_string(GetCurrentProcessId()));
    bool native_prediction_request=native_a_request || native_direction_request || (full_request && token=="OFFLINE_NATIVE_PREDICTION "+std::to_string(GetCurrentProcessId()));
    bool native_two_request=full_request && token=="OFFLINE_NATIVE_TWO_IDLE_REPLAY "+std::to_string(GetCurrentProcessId());
    bool native_replay_request=full_request && token=="OFFLINE_NATIVE_IDLE_REPLAY "+std::to_string(GetCurrentProcessId());
    bool field_only_request=full_request && token=="OFFLINE_FIELD_TRACE_ONLY "+std::to_string(GetCurrentProcessId());
    bool six_immediate_request=full_request && token=="OFFLINE_SIX_SYNCHRONOUS_REPLAY "+std::to_string(GetCurrentProcessId());
    bool three_immediate_request=six_immediate_request || (full_request && token=="OFFLINE_THREE_SYNCHRONOUS_REPLAY "+std::to_string(GetCurrentProcessId()));
    bool immediate_request=full_request && token=="OFFLINE_SINGLE_SYNCHRONOUS_REPLAY "+std::to_string(GetCurrentProcessId());
    bool native_idle_request=field_only_request || three_immediate_request || immediate_request || repeated_movement_request || native_movement_request || native_prediction_request || native_two_request || native_replay_request || (full_request && token=="OFFLINE_NATIVE_ADJACENT_IDLE "+std::to_string(GetCurrentProcessId()));
    bool reversal_structure_request=full_request && token=="OFFLINE_REVERSAL_STRUCTURE "+std::to_string(GetCurrentProcessId());
    bool three_reversal_request=full_request && token=="OFFLINE_THREE_FRAME_REVERSAL_INPUTS "+std::to_string(GetCurrentProcessId());
    bool three_edge_request=full_request && token=="OFFLINE_THREE_FRAME_EDGE_INPUTS "+std::to_string(GetCurrentProcessId());
    bool attack_structure_request=full_request && token=="OFFLINE_ATTACK_STRUCTURE "+std::to_string(GetCurrentProcessId());
    bool three_attack_request=full_request && token=="OFFLINE_THREE_FRAME_ATTACK_INPUTS "+std::to_string(GetCurrentProcessId());
    bool three_charge_request=full_request && token=="OFFLINE_THREE_FRAME_CHARGE_INPUTS "+std::to_string(GetCurrentProcessId());
    bool charge_inputs_request=three_charge_request || full_request && token=="OFFLINE_TWO_FRAME_CHARGE_INPUTS "+std::to_string(GetCurrentProcessId());
    bool two_charge_request=charge_inputs_request || full_request && token=="OFFLINE_TWO_FRAME_CHARGE "+std::to_string(GetCurrentProcessId());
    bool charge_replay_request=two_charge_request || full_request && token=="OFFLINE_ONE_FRAME_CHARGE "+std::to_string(GetCurrentProcessId());
    bool charge_trajectory_request=full_request && token=="OFFLINE_CHARGE_TRAJECTORY "+std::to_string(GetCurrentProcessId());
    bool three_input_request=full_request && token=="OFFLINE_THREE_FRAME_THROW_INPUTS "+std::to_string(GetCurrentProcessId());
    bool recorded_input_request=three_input_request || full_request && token=="OFFLINE_TWO_FRAME_THROW_INPUTS "+std::to_string(GetCurrentProcessId());
    bool two_throw_request=recorded_input_request || full_request && token=="OFFLINE_TWO_FRAME_THROW "+std::to_string(GetCurrentProcessId());
    bool two_frame_request=three_reversal_request || three_edge_request || three_attack_request || charge_replay_request || two_throw_request || (full_request && token=="OFFLINE_TWO_FRAME_IDLE "+std::to_string(GetCurrentProcessId()));
    bool long_trajectory_request=full_request && token=="OFFLINE_THROW_TRAJECTORY_LONG "+std::to_string(GetCurrentProcessId());
    bool throw_trajectory_request=long_trajectory_request || (full_request && token=="OFFLINE_THROW_TRAJECTORY "+std::to_string(GetCurrentProcessId()));
    bool trajectory_request=reversal_structure_request || attack_structure_request || charge_trajectory_request || throw_trajectory_request || two_frame_request || (full_request && token=="OFFLINE_IDLE_TRAJECTORY "+std::to_string(GetCurrentProcessId()));
    bool delayed_request=full_request && token=="OFFLINE_DELAY_RESTORE "+std::to_string(GetCurrentProcessId());
    bool throw_audit_request=full_request && token=="OFFLINE_THROW_AUDIT "+std::to_string(GetCurrentProcessId());
    bool audit_request=full_request && token=="OFFLINE_DELAY_AUDIT "+std::to_string(GetCurrentProcessId());
    bool reversal_request=full_request && token=="OFFLINE_REVERSAL_PRESERVE "+std::to_string(GetCurrentProcessId());
    bool edge_request=full_request && token=="OFFLINE_EDGE_PRESERVE "+std::to_string(GetCurrentProcessId());
    bool attack_request=full_request && token=="OFFLINE_ATTACK_PRESERVE "+std::to_string(GetCurrentProcessId());
    bool charge_request=full_request && token=="OFFLINE_CHARGE_PRESERVE "+std::to_string(GetCurrentProcessId());
    bool preserve_request=reversal_request || edge_request || attack_request || charge_request || throw_request || (full_request && token=="OFFLINE_FULL_PRESERVE "+std::to_string(GetCurrentProcessId()));
    if(token!=(composed_a_request?"OFFLINE_COMPOSED_A_CORRECTION ":immediate_a_request?"OFFLINE_IMMEDIATE_A_CORRECTION ":six_immediate_request?"OFFLINE_SIX_SYNCHRONOUS_REPLAY ":field_only_request?"OFFLINE_FIELD_TRACE_ONLY ":three_immediate_request?"OFFLINE_THREE_SYNCHRONOUS_REPLAY ":immediate_request?"OFFLINE_SINGLE_SYNCHRONOUS_REPLAY ":native_a_restore_request?"OFFLINE_NATIVE_A_RESTORE ":native_a_request?"OFFLINE_NATIVE_A_CAPTURE ":native_left_request?"OFFLINE_NATIVE_LEFT_MOVEMENT ":repeated_movement_request?"OFFLINE_REPEATED_MOVEMENT ":native_three_movement_request?"OFFLINE_NATIVE_THREE_MOVEMENT ":native_movement_request?"OFFLINE_NATIVE_TWO_MOVEMENT ":native_two_direction_request?"OFFLINE_NATIVE_TWO_DIRECTION_CAPTURE ":native_direction_restore_request?"OFFLINE_NATIVE_DIRECTION_RESTORE ":native_direction_request?"OFFLINE_NATIVE_DIRECTION ":native_prediction_request?"OFFLINE_NATIVE_PREDICTION ":native_two_request?"OFFLINE_NATIVE_TWO_IDLE_REPLAY ":native_replay_request?"OFFLINE_NATIVE_IDLE_REPLAY ":native_idle_request?"OFFLINE_NATIVE_ADJACENT_IDLE ":reversal_structure_request?"OFFLINE_REVERSAL_STRUCTURE ":three_reversal_request?"OFFLINE_THREE_FRAME_REVERSAL_INPUTS ":three_edge_request?"OFFLINE_THREE_FRAME_EDGE_INPUTS ":attack_structure_request?"OFFLINE_ATTACK_STRUCTURE ":three_attack_request?"OFFLINE_THREE_FRAME_ATTACK_INPUTS ":three_charge_request?"OFFLINE_THREE_FRAME_CHARGE_INPUTS ":charge_inputs_request?"OFFLINE_TWO_FRAME_CHARGE_INPUTS ":two_charge_request?"OFFLINE_TWO_FRAME_CHARGE ":charge_replay_request?"OFFLINE_ONE_FRAME_CHARGE ":charge_trajectory_request?"OFFLINE_CHARGE_TRAJECTORY ":three_input_request?"OFFLINE_THREE_FRAME_THROW_INPUTS ":recorded_input_request?"OFFLINE_TWO_FRAME_THROW_INPUTS ":long_trajectory_request?"OFFLINE_THROW_TRAJECTORY_LONG ":two_throw_request?"OFFLINE_TWO_FRAME_THROW ":throw_trajectory_request?"OFFLINE_THROW_TRAJECTORY ":two_frame_request?"OFFLINE_TWO_FRAME_IDLE ":trajectory_request?"OFFLINE_IDLE_TRAJECTORY ":combat_later_request?"OFFLINE_THROW_REPLAY_LATER ":combat_replay_request?"OFFLINE_THROW_REPLAY ":throw_audit_request?"OFFLINE_THROW_AUDIT ":delayed_request?"OFFLINE_DELAY_RESTORE ":audit_request?"OFFLINE_DELAY_AUDIT ":attack_observe_request?"OFFLINE_ATTACK_OBSERVE ":observe_request?"OFFLINE_CHARGE_OBSERVE ":reversal_request?"OFFLINE_REVERSAL_PRESERVE ":edge_request?"OFFLINE_EDGE_PRESERVE ":attack_request?"OFFLINE_ATTACK_PRESERVE ":charge_request?"OFFLINE_CHARGE_PRESERVE ":throw_request?"OFFLINE_THROW_PRESERVE ":preserve_request?"OFFLINE_FULL_PRESERVE ":full_request?"OFFLINE_FULL_RESTORE ":shared_request?"OFFLINE_SHARED_RESTORE ":save_request?"OFFLINE_SAVE ":"OFFLINE_TRAINING ")+std::to_string(GetCurrentProcessId())){status("rejected stale/invalid arm");return;}
    if(full_request && r.full_used){status("full restore already used this process; restart required");return;}
    if(full_request && (shared_request || save_request)){status("conflicting restore requests rejected");return;}
    if(shared_request && r.shared_used){status("shared restore already used this process; restart required");return;}
    if(shared_request && save_request){status("conflicting arm requests rejected");return;}
    if(!r.hooked){
        // Exact prologue from the verified build; reject other hooks at this site.
        const unsigned char expected[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x10,0x49,0x89,0x6b,0x18};
        unsigned char actual[sizeof(expected)]{};
        void* target=reinterpret_cast<void*>(r.base+0x2DBC60);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || memcmp(actual,expected,sizeof(actual))){status("prologue mismatch; no hook installed");return;}
        auto initialized=MH_Initialize();
        if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED){status("hook initialization failed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6EntryBridge),&SC6Original)!=MH_OK){status("hook creation failed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("hook enable failed");return;}
        r.hooked=true;
    }
    if((three_immediate_request || field_only_request) && !r.field_hooked){
        const unsigned char expected[]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x304D50);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("field trace prologue mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6FieldBridge),&SC6FieldOriginal)!=MH_OK){status("field trace hook creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("field trace hook enable failed; not armed");return;}
        r.field_hooked=true;
    }
    if((three_immediate_request || field_only_request) && !r.reset_hooked){
        const unsigned char expected[]={0x48,0x85,0xc9,0x0f,0x84};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x2D6D70);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("reset trace prologue mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6ResetBridge),&SC6ResetOriginal)!=MH_OK){status("reset trace hook creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("reset trace hook enable failed; not armed");return;}
        r.reset_hooked=true;
    }
    if(field_only_request && !r.after_hooked){
        const unsigned char expected[]={0xE9,0xEB,0x27,0x00,0x00};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x3793DC);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("post-call trace bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6AfterBridge),&SC6AfterOriginal)!=MH_OK){status("post-call trace hook creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("post-call trace hook enable failed; not armed");return;}
        r.after_hooked=true;
    }
    if(field_only_request && !r.battle_after_hooked){
        const unsigned char expected[]={0x48,0x8D,0x8B,0x30,0x0A,0x00,0x00};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x3FE816);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("battle-return trace bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6BattleAfterBridge),&SC6BattleAfterOriginal)!=MH_OK){status("battle-return trace hook creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("battle-return trace hook enable failed; not armed");return;}
        r.battle_after_hooked=true;
    }
    if(field_only_request && !r.cleanup_hooked){
        const unsigned char expected[]={0x48,0x8B,0xCB,0xE8,0x56,0xE6,0xFF,0xFF};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x3fe822);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("Cleanup checkpoint bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6CleanupAfterBridge),&SC6CleanupAfterOriginal)!=MH_OK){status("Cleanup checkpoint creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("Cleanup checkpoint enable failed; not armed");return;}
        r.cleanup_hooked=true;
    }
    if(field_only_request && !r.events_hooked){
        const unsigned char expected[]={0x48,0x8B,0xCB,0xE8,0x2E,0xE2,0xFF,0xFF};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x3fe82a);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("Events checkpoint bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6EventsAfterBridge),&SC6EventsAfterOriginal)!=MH_OK){status("Events checkpoint creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("Events checkpoint enable failed; not armed");return;}
        r.events_hooked=true;
    }
    if(field_only_request && !r.tail_hooked){
        const unsigned char expected[]={0x80,0xBB,0x62,0x14,0x00,0x00,0x00};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x3fe832);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("Tail checkpoint bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6TailAfterBridge),&SC6TailAfterOriginal)!=MH_OK){status("Tail checkpoint creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("Tail checkpoint enable failed; not armed");return;}
        r.tail_hooked=true;
    }
    if((field_only_request || three_immediate_request || immediate_a_request) && !r.setter_zero_hooked){
        const unsigned char expected[]={0x48,0x8B,0x01,0xFF,0x90,0x10,0x01,0x00,0x00};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x428334);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("setter checkpoint bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6SetterZeroBridge),&SC6SetterZeroOriginal)!=MH_OK){status("setter checkpoint creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("setter checkpoint enable failed; not armed");return;}
        r.setter_zero_hooked=true;
    }
    if((field_only_request || three_immediate_request || immediate_a_request) && !r.setter_full_hooked){
        const unsigned char expected[]={0x48,0x8B,0x01,0xFF,0x90,0x10,0x01,0x00,0x00};
        unsigned char actual[sizeof(expected)]{};void* target=reinterpret_cast<void*>(r.base+0x4283b9);
        if(!copy(actual,reinterpret_cast<uintptr_t>(target),sizeof(actual)) || std::memcmp(actual,expected,sizeof(actual))){status("setter checkpoint bytes mismatch; not armed");return;}
        if(MH_CreateHook(target,reinterpret_cast<void*>(&SC6SetterFullBridge),&SC6SetterFullOriginal)!=MH_OK){status("setter checkpoint creation failed; not armed");return;}
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);status("setter checkpoint enable failed; not armed");return;}
        r.setter_full_hooked=true;
    }
    AcquireSRWLockExclusive(&r.lock);
    r.field_only=field_only_request;
    r.charge_observe=observe_request;r.attack_observe=attack_observe_request;r.observation_started=false;
    r.count=0;r.skipped=0;r.owner=0;r.fault=false;r.reason="recording";r.deadline=now+20000;
    r.first_return_address=0;r.caller_changes=0;
    r.save=save_request?std::make_unique<sc6::research::SaveProbe>():nullptr;
    r.shared=shared_request?std::make_unique<sc6::research::SharedRestoreProbe>():nullptr;
    if(shared_request)r.shared_used=true;
    r.two_frame=two_frame_request?std::make_unique<sc6::research::TwoFrameIdleReplay>(two_throw_request,(three_reversal_request || three_edge_request || three_attack_request || three_charge_request)?3u:two_charge_request?2u:charge_replay_request?1u:three_input_request?3u:2u,charge_replay_request,three_attack_request,three_edge_request,three_reversal_request):nullptr;
    r.trajectory=trajectory_request?std::make_unique<sc6::research::IdleTrajectoryAudit>(throw_trajectory_request || two_throw_request,(reversal_structure_request || three_reversal_request || three_edge_request || attack_structure_request || three_attack_request || three_charge_request)?4u:two_charge_request?3u:charge_replay_request?2u:(charge_trajectory_request || long_trajectory_request || three_input_request)?4u:3u,charge_trajectory_request || charge_replay_request,three_attack_request || attack_structure_request,attack_structure_request || reversal_structure_request,three_edge_request,three_reversal_request || reversal_structure_request):nullptr;
    if(r.trajectory){r.trajectory->throw_mode=throw_trajectory_request || two_throw_request;r.trajectory->recorded_inputs=recorded_input_request || charge_inputs_request || three_attack_request || three_edge_request || three_reversal_request;r.trajectory->charge_mode=charge_trajectory_request || charge_replay_request;}
    r.repeated_movement=repeated_movement_request?std::make_unique<sc6::research::RepeatedMovement>():nullptr;
    r.native_movement=native_movement_request?std::make_unique<sc6::research::NativeTwoMovement>((native_left_request || native_three_movement_request)?3u:2u,native_left_request?0x800u:0x400u):nullptr;
    r.native_prediction=native_prediction_request?std::make_unique<sc6::research::NativePredictionProbe>(native_direction_request,native_direction_restore_request,native_two_direction_request,native_a_request,native_a_restore_request,immediate_a_request,composed_a_request):nullptr;
    r.native_two=native_two_request?std::make_unique<sc6::research::NativeTwoFrameReplay>():nullptr;
    r.three_immediate=three_immediate_request?std::make_unique<sc6::research::NativeThreeImmediateReplay>(six_immediate_request?6u:3u):nullptr;
    r.immediate_replay=immediate_request?std::make_unique<sc6::research::NativeImmediateReplay>():nullptr;
    r.native_replay=native_replay_request;
    r.native_source=(native_idle_request && !field_only_request && !three_immediate_request && !immediate_request && !repeated_movement_request && !native_movement_request && !native_two_request && !native_prediction_request)?std::make_unique<sc6::research::NativeBoundarySnapshot>(sc6::research::NativeBoundarySnapshot::Mode::idle):nullptr;
    r.native_restore=(native_idle_request && !field_only_request && !three_immediate_request && !immediate_request && !repeated_movement_request && !native_movement_request && !native_two_request && !native_prediction_request)?std::make_unique<sc6::research::NativeAdjacentRestore>():nullptr;
    r.full=(full_request && !trajectory_request && !native_idle_request)?std::make_unique<sc6::research::FullStateProbe>():nullptr;
    if(r.full){r.full->combat_flag_requested=throw_request;r.full->combat_load_diagnostic=throw_request;r.full->throw_audit=throw_audit_request;r.full->delayed_requested=delayed_request;r.full->camera_restore_requested=delayed_request;r.full->delay_audit=throw_audit_request || audit_request || delayed_request;r.full->ring_capture_enabled=throw_request || throw_audit_request || audit_request || delayed_request;r.full->preserve_requested=preserve_request || delayed_request;r.full->throw_requested=throw_request || throw_audit_request;r.full->charge_requested=charge_request;r.full->attack_requested=attack_request;r.full->edge_requested=edge_request;r.full->reversal_requested=reversal_request;}
    if(r.full && combat_replay_request){
        r.full->combat_later_window=combat_later_request;r.full->combat_replay_requested=true;r.full->throw_audit=true;r.full->throw_requested=true;
        r.full->delayed_requested=true;r.full->delay_audit=true;r.full->ring_capture_enabled=true;
        r.full->preserve_requested=true;r.full->camera_restore_requested=true;r.full->combat_flag_requested=true;
    }
    if(reversal_structure_request || three_reversal_request || three_edge_request || attack_structure_request || three_attack_request || charge_replay_request || charge_trajectory_request || two_throw_request || throw_trajectory_request || combat_replay_request || throw_audit_request || attack_observe_request || observe_request || throw_request || charge_request || attack_request || edge_request || reversal_request)r.deadline=now+45000;
    r.full_fields_equal=false;
    if(full_request && !field_only_request)r.full_used=true;
    r.stem=(native_idle_request?"native-adjacent-idle-":reversal_structure_request?"reversal-structure-":three_reversal_request?"three-frame-reversal-inputs-":three_edge_request?"three-frame-edge-inputs-":attack_structure_request?"attack-structure-":three_attack_request?"three-frame-attack-inputs-":three_charge_request?"three-frame-charge-inputs-":charge_inputs_request?"two-frame-charge-inputs-":two_charge_request?"two-frame-charge-":charge_replay_request?"one-frame-charge-":charge_trajectory_request?"charge-trajectory-":three_input_request?"three-frame-throw-inputs-":recorded_input_request?"two-frame-throw-inputs-":long_trajectory_request?"throw-trajectory-long-":two_throw_request?"two-frame-throw-":throw_trajectory_request?"throw-trajectory-":two_frame_request?"two-frame-idle-":trajectory_request?"idle-trajectory-":combat_later_request?"throw-replay-later-":combat_replay_request?"throw-replay-":throw_audit_request?"throw-audit-":delayed_request?"delay-restore-":audit_request?"delay-audit-":attack_observe_request?"attack-observe-":observe_request?"charge-observe-":reversal_request?"reversal-preserve-":edge_request?"edge-preserve-":attack_request?"attack-preserve-":charge_request?"charge-preserve-":throw_request?"throw-preserve-":preserve_request?"full-preserve-":full_request?"full-restore-":shared_request?"shared-restore-":save_request?"save-":"frames-")+std::to_string(GetCurrentProcessId())+"-"+std::to_string(now);
    r.active.store(true,std::memory_order_release);ReleaseSRWLockExclusive(&r.lock);
    status(field_only_request?"armed: 600-frame field trace only; no restoration or extra updates":three_immediate_request?"armed: three synchronous offline idle replay frames; leave controls untouched":immediate_request?"armed: one synchronous offline idle replay; leave controls untouched":native_idle_request?"armed: one-frame idle restore from live context; leave controls untouched":reversal_structure_request?"armed: Reversal Edge four-state observation; no restoration":three_reversal_request?"armed: guarded three-update Reversal Edge clash replay":three_edge_request?"armed: guarded three-update Critical Edge replay; land attack and release controls":attack_structure_request?"armed: Soul Attack four-state structural observation; no restoration":three_attack_request?"armed: guarded three-update Soul Attack replay after startup camera creation; land attack and release controls":three_charge_request?"armed: three-update Soul Charge replay with recorded release inputs":charge_inputs_request?"armed: two-update Soul Charge replay with recorded release inputs":two_charge_request?"armed: guarded two-update Soul Charge replay; release controls after activation":charge_replay_request?"armed: guarded one-update Soul Charge replay; release controls after activation":charge_trajectory_request?"armed: Soul Charge four reference states, no restoration":three_input_request?"armed: A+G three-update replay with recorded auxiliary inputs":recorded_input_request?"armed: A+G two-update replay with recorded auxiliary inputs":long_trajectory_request?"armed: waiting for A+G; four reference states, no restore":two_throw_request?"armed: waiting for A+G; guarded two-update combat replay":throw_trajectory_request?"armed: waiting for A+G; three reference states, no restore":two_frame_request?"armed: guarded two-update idle rewind/replay; leave controls untouched":trajectory_request?"armed: three idle reference boundaries; no restore; leave controls untouched":combat_replay_request?"armed: guarded one-frame A+G replay; waiting for throw":throw_audit_request?"armed: waiting for connected A+G; two adjacent saves, no restore":delayed_request?"armed: one-frame idle restore experiment; leave controls untouched":audit_request?"armed: two adjacent idle saves, then observation; no restore":reversal_request?"armed: waiting up to 45 seconds for Reversal Edge clash slowdown, then 600 entries":edge_request?"armed: waiting up to 45 seconds for Critical Edge startup, then 600 entries":attack_request?"armed: waiting up to 45 seconds for Soul Attack startup, then 600 entries":attack_observe_request?"armed: observation only, waiting for Soul Attack then 600 entries; no save/load":observe_request?"armed: observation only, waiting for Soul Charge then 600 entries; no save/load":charge_request?"armed: waiting up to 45 seconds for Soul Charge freeze with active auxiliary mode 3, then 600 entries":throw_request?"armed: waiting up to 45 seconds for connected A+G freeze, then 600 entries":"armed: 600 entries or 20 seconds; offline Training must remain active");
}
void recorder_stop(){sc6::research::lease_service::request_cancel();auto& r=state();r.active.store(false);/* Pinned passive bridge stays valid until exit. */}
}




