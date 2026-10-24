#pragma once
#include <Windows.h>
#include <cstdint>
#include <limits>
#include <array>
namespace sc6::research {
// Caller must own the generated buffer. Use allocator byte length, never a
// serializer relocation extent. Equality is diagnostic, not immutability proof.
struct GeneratedBufferFingerprint {
 uintptr_t buffer{},size{};uint64_t hash{};bool valid{};
 static GeneratedBufferFingerprint read(uintptr_t buffer,uintptr_t size){
  GeneratedBufferFingerprint value{};value.buffer=buffer;value.size=size;
  if(!buffer || !size || size>1024*1024 || buffer>(std::numeric_limits<uintptr_t>::max)()-size)return value;
  __try {value.hash=14695981039346656037ull;
   const auto bytes=reinterpret_cast<const volatile unsigned char*>(buffer);
   for(uintptr_t i=0;i<size;++i){value.hash^=bytes[i];value.hash*=1099511628211ull;}value.valid=true;
  }__except(EXCEPTION_EXECUTE_HANDLER){value.valid=false;}return value;
 }
 bool matches(const GeneratedBufferFingerprint& other)const{return valid && other.valid && buffer==other.buffer && size==other.size && hash==other.hash;}
};
// One read-only validation batch only; never retain across native mutations.
template<unsigned Capacity> struct GeneratedBufferValidationBatch {
 std::array<GeneratedBufferFingerprint,Capacity> observed{};unsigned count{},reads{},reuses{};
 bool matches(const GeneratedBufferFingerprint& expected){
  if(!expected.valid)return false;
  for(unsigned i=0;i<count;++i)if(observed[i].buffer==expected.buffer && observed[i].size==expected.size){
   ++reuses;return expected.matches(observed[i]);
  }
  const auto live=GeneratedBufferFingerprint::read(expected.buffer,expected.size);++reads;
  if(count<Capacity)observed[count++]=live;
  return expected.matches(live);
 }
};
}
