#pragma once
#include <array>
#include <cstdint>
namespace sc6::research {
// Fixed-size, owner-thread evidence. No allocation, formatting or file I/O.
// Each observation records a scalar read or a virtual-method comparison.
struct SnapshotPreflightDiagnostic {
    static constexpr unsigned capacity=32,header_count=8,record_words=5;
    // Append without moving the original observation ring. Per-side attachment
    // evidence survives later component reads and preserves the rejected entry.
    static constexpr unsigned attachment_begin=header_count+capacity*record_words;
    enum AttachmentField : unsigned {attachment_count,attachment_capacity,attachment_live,attachment_expired,
        attachment_entry,attachment_object,attachment_reason,attachment_vtable,attachment_fields};
    static constexpr unsigned word_count=attachment_begin+2*attachment_fields;
    std::array<uint64_t,word_count> words{};
    void phase(unsigned value){words[0]=value;}
    void location(unsigned line){words[1]=line;}
    bool reject(unsigned line){words[1]=line;words[2]=1;return false;}
    void exception(unsigned code){words[2]=1;words[3]=code;}
    void pending(uintptr_t address){words[4]=address;}
    void record(unsigned kind,uintptr_t address,uint64_t actual,uint64_t expected,unsigned size){
        auto n=words[5]++;auto k=header_count+(n%capacity)*record_words;
        words[k]=kind;words[k+1]=address;words[k+2]=actual;words[k+3]=expected;words[k+4]=size;
    }
};
struct SnapshotPreflightTrace {
    SnapshotPreflightDiagnostic* diagnostic{};
    explicit SnapshotPreflightTrace(SnapshotPreflightDiagnostic* d,unsigned phase):diagnostic(d){if(d)d->phase(phase);}
    template<class T> T read(uintptr_t address){
        if(diagnostic)diagnostic->pending(address);
        auto v=*reinterpret_cast<const T*>(address);
        if(diagnostic)diagnostic->record(1,address,static_cast<uint64_t>(v),0,sizeof(T));
        return v;
    }
    uintptr_t ptr(uintptr_t address){return read<uintptr_t>(address);}
    bool method(uintptr_t object,unsigned slot,uintptr_t base,unsigned rva){
        if(diagnostic)diagnostic->words[7]=object;
        if(object<0x10000){if(diagnostic)diagnostic->record(3,object,object,0x10000,slot);return false;}
        auto address=ptr(object)+slot;auto actual=ptr(address);
        if(diagnostic)diagnostic->record(2,address,actual,base+rva,slot);
        return actual==base+rva;
    }
    void location(unsigned line){if(diagnostic)diagnostic->location(line);}
    void attachment(unsigned side,unsigned field,uint64_t value){
        if(diagnostic && side<2 && field<SnapshotPreflightDiagnostic::attachment_fields)
            diagnostic->words[SnapshotPreflightDiagnostic::attachment_begin+side*SnapshotPreflightDiagnostic::attachment_fields+field]=value;
    }
    bool reject(unsigned line){return diagnostic?diagnostic->reject(line):false;}
};
}
