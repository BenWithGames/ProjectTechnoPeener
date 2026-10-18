#include "../src/MatchDataset.hpp"
#include <cassert>
#include <iostream>
int main(){
 namespace m=sc6::research::match_dataset;
 auto root=std::filesystem::temp_directory_path()/("sc6-dataset-test-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(root);
 auto arm=[&](unsigned pid,unsigned id){std::ofstream f(root/"record-match.arm");f<<pid<<' '<<id;};
 arm(GetCurrentProcessId(),1);m::poll(root,false);assert(!m::state.active);
 arm(0,1);m::poll(root,true);assert(!m::state.active);
 for(unsigned n=1;n<=3;++n){
  arm(GetCurrentProcessId(),n);m::poll(root,true);assert(m::state.active && m::recording && m::state.id==n);
  auto& s=m::state;s.records[0].frame=101;s.records[0].rng=123;s.records[0].inputs[0]=std::byte{42};s.count=1;
  {std::ofstream stop(root/"record-match.stop");}
  m::poll(root,true);assert(!s.active && s.written && !m::recording);
  assert(std::filesystem::file_size(s.folder/"frames.bin")==sizeof(m::Record));
  std::ifstream f(s.folder/"frames.bin",std::ios::binary);m::Record record;f.read(reinterpret_cast<char*>(&record),sizeof(record));assert(record.frame==101 && record.rng==123 && record.inputs[0]==std::byte{42});
 }
 uint32_t frame=0,rng=0;assert(!m::read_counters(0,frame,rng));
 std::cout<<"PASS: three independent datasets, unsafe/stale arm rejection, bounded binary input export, explicit stop and no restore path\n";
}
