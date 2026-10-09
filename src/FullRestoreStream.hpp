#pragma once
#include "SaveProbe.hpp"
#include <intrin.h>
namespace sc6::research {
// Fresh in-memory streams only. An exception aborts native code on an invalid transfer;
// it cannot undo side effects that happened before the rejected read.
struct FullRestoreStream {
    SaveStream stream;
    static constexpr unsigned transfer_limit=4096;
    static constexpr DWORD rejection=0xE053C602;
    std::array<std::uint64_t,transfer_limit> lengths{};
    struct Trace { uintptr_t source{},destination{},writer{},reader{}; };
    std::array<Trace,transfer_limit> traces{};
    unsigned writes{},reads{}; bool sealed{};
    FullRestoreStream():stream(SaveProbe::guarded_copy){stream.slots=&table;}
    static FullRestoreStream* owner(SaveStream* s){return reinterpret_cast<FullRestoreStream*>(s);}
    static void reject(SaveStream* s,SaveStream::Error e){s->fail(e);RaiseException(rejection,0,0,nullptr);}
    static void* destroy(SaveStream* s,unsigned){reject(s,SaveStream::forbidden_operation);return s;}
    static void reset(SaveStream* s){reject(s,SaveStream::forbidden_operation);}
    static void seek(SaveStream* s,std::uint64_t){reject(s,SaveStream::forbidden_operation);}
    __declspec(noinline) static std::uint64_t write(SaveStream* s,const void* data,std::uint64_t n){
        auto* t=owner(s);
        if(t->sealed || t->writes==transfer_limit || !n){reject(s,SaveStream::forbidden_operation);return 0;}
        auto result=SaveStream::write(s,data,n);
        if(s->failures){RaiseException(rejection,0,0,nullptr);return 0;}
        t->traces[t->writes].source=reinterpret_cast<uintptr_t>(data);
        t->traces[t->writes].writer=reinterpret_cast<uintptr_t>(_ReturnAddress());
        t->lengths[t->writes++]=n;return result;
    }
    bool seal(){sealed=!stream.failures && writes && stream.write_position;return sealed;}
    __declspec(noinline) static std::uint64_t read(SaveStream* s,void* destination,std::uint64_t n){
        auto* t=owner(s);
        if(!t->sealed || s->failures || t->reads>=t->writes || n!=t->lengths[t->reads] ||
           s->read_position>s->write_position || n>s->write_position-s->read_position){
            reject(s,SaveStream::overflow);return 0;
        }
        if(!destination || !s->copier(destination,s->payload.data()+s->read_position,static_cast<size_t>(n))){
            reject(s,SaveStream::copy_fault);return 0;
        }
        t->traces[t->reads].destination=reinterpret_cast<uintptr_t>(destination);
        t->traces[t->reads].reader=reinterpret_cast<uintptr_t>(_ReturnAddress());
        s->read_position+=n;++t->reads;return n;
    }
    // Prepare a private reader without consuming or resetting the archived save.
    // Only pristine readers and complete, unread captures are accepted. Failed
    // or partly consumed streams must never be recycled as valid snapshots.
    bool prepare_reader_from(const FullRestoreStream& snapshot){
        if(this==&snapshot || sealed || writes || reads || stream.write_position ||
           stream.read_position || stream.failures || stream.transfers ||
           !snapshot.sealed || !snapshot.writes || snapshot.writes>transfer_limit ||
           snapshot.reads || snapshot.stream.read_position || snapshot.stream.failures ||
           snapshot.stream.first_error!=SaveStream::none ||
           snapshot.stream.slots!=&table || !snapshot.stream.copier ||
           snapshot.stream.write_position>SaveStream::limit)return false;
        std::uint64_t total=0;
        for(unsigned i=0;i<snapshot.writes;++i){
            auto length=snapshot.lengths[i];
            if(!length || length>snapshot.stream.write_position-total)return false;
            total+=length;
        }
        if(total!=snapshot.stream.write_position)return false;
        *this=snapshot;
        for(unsigned i=0;i<writes;++i){traces[i].destination=0;traces[i].reader=0;}
        return true;
    }
    bool consumed()const{return sealed && !stream.failures && reads==writes && stream.read_position==stream.write_position;}
    inline static const SaveStreamSlots table={destroy,SaveStream::capacity,reset,seek,seek,write,read,SaveStream::tell_write,SaveStream::tell_read};
};
static_assert(std::is_standard_layout_v<FullRestoreStream> && offsetof(FullRestoreStream,stream)==0);
}
