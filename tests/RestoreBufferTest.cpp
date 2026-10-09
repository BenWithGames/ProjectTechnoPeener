#include "../src/RestoreBuffer.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
using sc6::research::RestoreBuffer;
void check(bool b){if(!b) std::exit(1);}
template<class F> void rejected(F action,RestoreBuffer::Error expected){
    try {action();} catch(const RestoreBuffer::Failure& e){check(e.reason==expected);return;} std::exit(2);
}
bool failed_copy(void*,const void*,std::size_t){return false;}
int main(){
    const std::array<std::byte,4> source={std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
    std::array<std::byte,6> out{};
    RestoreBuffer good(source);good.read(out.data()+1,2);good.read(out.data()+3,2);good.finish();
    check(out.front()==std::byte{} && out.back()==std::byte{} && good.position()==4 && good.transfers()==2);
    check(std::memcmp(out.data()+1,source.data(),4)==0);
    RestoreBuffer short_input(source);auto before=out;
    rejected([&]{short_input.read(out.data(),5);},RestoreBuffer::Error::overread);
    check(out==before && short_input.position()==0);
    rejected([&]{short_input.read(out.data(),1);},RestoreBuffer::Error::overread);
    RestoreBuffer wrap(source); rejected([&]{wrap.read(out.data(),std::numeric_limits<std::uint64_t>::max());},RestoreBuffer::Error::overread);
    RestoreBuffer null(source);rejected([&]{null.read(nullptr,1);},RestoreBuffer::Error::null_destination);
    RestoreBuffer copy(source,failed_copy);rejected([&]{copy.read(out.data(),1);},RestoreBuffer::Error::copy_failed);check(copy.position()==0);
    RestoreBuffer incomplete(source);incomplete.read(out.data(),1);rejected([&]{incomplete.finish();},RestoreBuffer::Error::incomplete);
    RestoreBuffer empty({});empty.read(nullptr,0);empty.finish();
    std::puts("PASS: exact consumption, guards, truncation, overflow, sticky failure, null destination, copy failure, incomplete stream");
}
