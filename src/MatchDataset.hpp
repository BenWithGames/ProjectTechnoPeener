#pragma once
#include <Windows.h>
#ifdef interface
#undef interface
#endif
#include <cstdint>
#include <cstring>
#include <atomic>
#include "NativeBoundarySnapshot.hpp"
#include "RoundEventObserver.hpp"
#include "ReplayFailureEvidence.hpp"
#include <vector>
#include <fstream>
#include <filesystem>
namespace sc6::research::match_dataset {
constexpr unsigned capacity=54000,snapshot_count=8;
struct Record {uint64_t qpc{},generation{};uint32_t frame{},round{},event{},rng{};std::array<std::byte,40> inputs{};};
struct State {
 SRWLOCK lock=SRWLOCK_INIT;bool active{},written{};unsigned count{},invalid{},overflow{},owner{},id{};std::atomic<unsigned> skipped{};
 std::vector<Record> records;
 std::array<std::unique_ptr<NativeBoundarySnapshot>,snapshot_count> snapshots;
 std::array<unsigned,snapshot_count> indices{};
 std::filesystem::path folder;
};
inline State state;
inline std::atomic<bool> recording{};
inline bool read_counters(uintptr_t base,uint32_t& frame,uint32_t& rng) noexcept {
 __try{frame=*reinterpret_cast<const uint32_t*>(base+0x470D0C4);rng=*reinterpret_cast<const uint32_t*>(base+0x485EB28);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void frame(uintptr_t base,uintptr_t argument) noexcept {
 auto& s=state;if(!TryAcquireSRWLockExclusive(&s.lock)){if(recording.load())++s.skipped;return;}
 struct Unlock{~Unlock(){ReleaseSRWLockExclusive(&state.lock);}} unlock;
 if(!s.active)return;
 try {
  if(s.owner && s.owner!=GetCurrentThreadId()){++s.invalid;s.active=false;return;}s.owner=GetCurrentThreadId();
  if(s.count==capacity){++s.overflow;s.active=false;return;}
  Record r;LARGE_INTEGER q;QueryPerformanceCounter(&q);r.qpc=q.QuadPart;
  auto marker=sc6::round_event::snapshot();r.generation=marker.generation;r.round=marker.round;r.event=marker.counter;
  if(!marker.generation || !marker.round)return;
  if(!FullStateProbe::input_bytes(argument,r.inputs)){++s.invalid;return;}
  if(!read_counters(base,r.frame,r.rng)){++s.invalid;return;}
  s.records[s.count]=r;
  // Two bounded four-frame reference windows. No native loader or replay calls.
  unsigned index=snapshot_count;
  if(s.count>=120 && s.count<124)index=s.count-120;
  if(s.count>=1800 && s.count<1804)index=4+s.count-1800;
  if(index<snapshot_count){s.indices[index]=s.count;s.snapshots[index]->capture(base,argument);}
  ++s.count;
 }catch(...){++s.invalid;s.active=false;}
}
inline void save(State& s){
 std::ofstream frames(s.folder/"frames.bin",std::ios::binary);
 frames.write(reinterpret_cast<const char*>(s.records.data()),s.count*sizeof(Record));frames.close();
 std::ofstream csv(s.folder/"frames.csv");csv<<"index,qpc,generation,round,event,frame,rng";for(unsigned i=0;i<10;++i)csv<<",input_word_"<<i;csv<<'\n';
 for(unsigned n=0;n<s.count;++n){const auto& r=s.records[n];csv<<n<<','<<r.qpc<<','<<r.generation<<','<<r.round<<','<<r.event<<','<<r.frame<<','<<r.rng;for(unsigned i=0;i<10;++i){uint32_t w;std::memcpy(&w,r.inputs.data()+i*4,4);csv<<','<<w;}csv<<'\n';}csv.close();
 unsigned captured=0,written=0,windows=0,gaps=0;
 for(unsigned n=1;n<s.count;++n){const auto& a=s.records[n-1];const auto& b=s.records[n];if(a.generation==b.generation && a.round==b.round && a.event==b.event && b.frame!=a.frame+1)++gaps;}
 for(unsigned start=0;start<snapshot_count;start+=4){bool valid=true;for(unsigned j=0;j<4;++j){const auto& b=*s.snapshots[start+j];valid=valid && b.valid;if(j){const auto& a=s.records[s.indices[start+j-1]];const auto& r=s.records[s.indices[start+j]];valid=valid && a.generation==r.generation && a.round==r.round && a.event==r.event && r.frame==a.frame+1;}}windows+=valid;}
 for(unsigned i=0;i<snapshot_count;++i){const auto& b=*s.snapshots[i];if(!b.attempted)continue;
  ReplayFailureEvidence evidence;const uint64_t magic=0x3144564543364353ULL;evidence.append(&magic,8);
  evidence.section("record_index",s.indices[i]);evidence.section("capture_valid",b.valid);evidence.section("capture_stage",b.capture_stage);evidence.section("exception",b.exception);
  evidence.section("module",b.module);evidence.section("input",b.data->delayed_inputs);evidence.section("measurements",b.data->measurements);evidence.section("keys",b.data->before_keys);
  evidence.section("native.saved.lengths",b.data->saved.lengths);evidence.section("native.saved.traces",b.data->saved.traces);evidence.section("native.saved.size",b.data->saved.stream.write_position);evidence.section("native.saved.payload",b.data->saved.stream.payload);
  evidence.section("rings",b.data->rings);evidence.section("blend",b.data->blends);evidence.section("camera",b.data->cameras);evidence.section("transforms",b.data->transforms);
  evidence.state=1;written+=evidence.write(s.folder/("snapshot-"+std::to_string(i)+".bin"));captured+=b.valid;
 }
 LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
 std::ofstream report(s.folder/"recording.txt");report<<"schema=1\nscope=normal_match_recording\nrestore=0\ninjection=0\nreplay=0\nobject_retention=0\nportable_replay=0\nmatch_boundary_source=operator\nstatus="<<(s.count && !s.invalid && !s.overflow && !s.skipped.load() && !gaps && frames && csv?"RECORDED":"INCOMPLETE")<<"\nrecords="<<s.count<<"\nrecord_bytes="<<sizeof(Record)<<"\nqpc_frequency="<<frequency.QuadPart<<"\ninvalid="<<s.invalid<<"\noverflow="<<s.overflow<<"\nskipped_callbacks="<<s.skipped.load()<<"\nframe_gaps="<<gaps<<"\nconsecutive_snapshot_windows="<<windows<<"\nsnapshots_valid="<<captured<<"\nsnapshots_written="<<written<<"\npid="<<GetCurrentProcessId()<<'\n';
 report.close();s.written=bool(report);
}
inline void poll(const std::filesystem::path& directory,bool safe){
 auto& s=state;const auto arm=directory/"record-match.arm",stop=directory/"record-match.stop";
 AcquireSRWLockExclusive(&s.lock);
 struct Unlock{~Unlock(){ReleaseSRWLockExclusive(&state.lock);}} unlock;
 if(std::filesystem::exists(stop)){std::filesystem::remove(stop);s.active=false;}
 recording=s.active;
 if(!s.active && !s.records.empty() && !s.written){save(s);return;}
 if(s.active || !std::filesystem::exists(arm))return;
 unsigned pid{},id{};std::ifstream request(arm);request>>pid>>id;request.close();std::filesystem::remove(arm);
 if(!safe || pid!=GetCurrentProcessId() || id<1 || id>3)return;
 s.folder=directory/("match-dataset-"+std::to_string(pid)+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(id));std::filesystem::create_directories(s.folder);
 s.records.resize(capacity);for(auto& p:s.snapshots)p=std::make_unique<NativeBoundarySnapshot>(NativeBoundarySnapshot::Mode::general_combat);
 s.count=s.invalid=s.overflow=s.owner=0;s.skipped=0;s.id=id;s.indices={};s.written=false;s.active=true;recording=true;
 std::ofstream ready(directory/"record-match-ready.txt");ready<<s.folder.string();
}
}
