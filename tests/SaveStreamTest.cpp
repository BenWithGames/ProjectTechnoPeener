#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/SaveProbe.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
using sc6::research::SaveStream;
void check(bool ok) { if(!ok) { std::fputs("Save stream test failed\n",stderr); std::exit(1); } }
bool reject_copy(void*,const void*,std::size_t) { return false; }
int main() {
    auto s=std::make_unique<SaveStream>();
    auto data=std::make_unique<std::array<std::byte,SaveStream::limit>>();
    data->fill(std::byte{0x5a});
    check(s->slots->capacity(s.get())==SaveStream::limit);
    check(s->slots->write(s.get(),data->data(),SaveStream::limit)==SaveStream::limit);
    check(s->payload==*data);
    check(s->slots->write(s.get(),data->data(),1)==0);
    check(s->first_error==SaveStream::overflow && s->write_position==SaveStream::limit);
    check(s->slots->write(s.get(),data->data(),0)==0 && s->transfers==3);
    s=std::make_unique<SaveStream>();
    check(s->slots->write(s.get(),data->data(),1)==1);
    check(s->slots->write(s.get(),data->data(),std::numeric_limits<std::uint64_t>::max())==0);
    check(s->write_position==1 && s->first_error==SaveStream::overflow);
    s=std::make_unique<SaveStream>();
    check(s->slots->write(s.get(),nullptr,4)==0 && s->first_error==SaveStream::null_source);
    s->slots->reset(s.get()); check(s->failures==2);
    s=std::make_unique<SaveStream>(reject_copy);
    check(s->slots->write(s.get(),data->data(),4)==0);
    check(s->write_position==0 && s->first_error==SaveStream::copy_fault);
    s=std::make_unique<SaveStream>();
    std::uint32_t untouched=0x12345678;
    check(s->slots->read(s.get(),&untouched,4)==0 && untouched==0x12345678);
    s->slots->set_write(s.get(),5); s->slots->set_read(s.get(),5);
    check(s->slots->destroy(s.get(),1)==s.get());
    check(s->failures==4 && s->slots->tell_write(s.get())==0 && s->slots->tell_read(s.get())==0);
    auto probe=std::make_unique<sc6::research::SaveProbe>();
    check(!probe->guarded_copy(&untouched,reinterpret_cast<void*>(1),4));
    probe->run(0);
    check(probe->attempted && probe->fault && !probe->invoked);
    auto elapsed=probe->elapsed_qpc;
    probe->run(0); check(probe->elapsed_qpc==elapsed);
    std::puts("PASS: ABI offsets, exact capacity, overflow, sticky failures, copy failure, forbidden read/reset/seek/delete");
}
