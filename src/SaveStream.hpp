#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace sc6::research {
// Offline harness only. Not connected to a game hook until dynamic serializers
// and their stream usage have been audited. Explicit slots avoid C++ vtable ABI assumptions.
struct SaveStream;
struct SaveStreamSlots {
    void* (*destroy)(SaveStream*, unsigned);
    std::uint64_t (*capacity)(SaveStream*);
    void (*reset)(SaveStream*);
    void (*set_write)(SaveStream*, std::uint64_t);
    void (*set_read)(SaveStream*, std::uint64_t);
    std::uint64_t (*write)(SaveStream*, const void*, std::uint64_t);
    std::uint64_t (*read)(SaveStream*, void*, std::uint64_t);
    std::uint64_t (*tell_write)(SaveStream*);
    std::uint64_t (*tell_read)(SaveStream*);
};
struct SaveStream {
    static constexpr std::size_t limit=0x28000;
    const SaveStreamSlots* slots;
    std::uint64_t write_position{}, read_position{};
    std::array<std::byte,limit> payload{};
    // Metadata follows the original object's layout. Reset calls cannot erase failures.
    std::uint64_t failures{}, transfers{};
    enum Error : unsigned { none, null_source, overflow, copy_fault, forbidden_operation };
    Error first_error=none;
    using Copier=bool (*)(void*,const void*,std::size_t);
    Copier copier;
    static bool local_copy(void* d,const void* s,std::size_t n) {
        std::memcpy(d,s,n); return true;
    }
    explicit SaveStream(Copier copy=local_copy):slots(&table),copier(copy) {}
    void fail(Error e) { ++failures; if(first_error==none) first_error=e; }
    static void* destroy(SaveStream* s,unsigned) { s->fail(forbidden_operation); return s; }
    static std::uint64_t capacity(SaveStream*) { return limit; }
    static void reset(SaveStream* s) { s->fail(forbidden_operation); }
    static void set_write(SaveStream* s,std::uint64_t) { s->fail(forbidden_operation); }
    static void set_read(SaveStream* s,std::uint64_t) { s->fail(forbidden_operation); }
    static std::uint64_t write(SaveStream* s,const void* source,std::uint64_t n) {
        ++s->transfers;
        // Sticky failure: a later successful-looking write must not hide a truncated save.
        if(s->failures) return 0;
        if(!source) { s->fail(null_source); return 0; }
        if(s->write_position>limit || n>limit-s->write_position) {
            s->fail(overflow); return 0;
        }
        if(!s->copier || !s->copier(s->payload.data()+s->write_position,source,static_cast<std::size_t>(n))) {
            s->fail(copy_fault); return 0;
        }
        s->write_position+=n; return n;
    }
    static std::uint64_t read(SaveStream* s,void*,std::uint64_t) {
        s->fail(forbidden_operation); return 0;
    }
    static std::uint64_t tell_write(SaveStream* s) { return s->write_position; }
    static std::uint64_t tell_read(SaveStream* s) { return s->read_position; }
    inline static const SaveStreamSlots table={destroy,capacity,reset,set_write,set_read,write,read,tell_write,tell_read};
};
static_assert(sizeof(void*)==8);
static_assert(std::is_standard_layout_v<SaveStream>);
static_assert(offsetof(SaveStream,write_position)==8);
static_assert(offsetof(SaveStream,read_position)==0x10);
static_assert(offsetof(SaveStream,payload)==0x18);
static_assert(offsetof(SaveStream,failures)==0x28018);
static_assert(offsetof(SaveStreamSlots,write)==0x28);
static_assert(offsetof(SaveStreamSlots,read)==0x30);
static_assert(offsetof(SaveStreamSlots,tell_read)==0x40);
}
