#pragma once
#include "SnapshotPreflightDiagnostic.hpp"
namespace sc6::research {
// Pinned native evidence: 3CAEB0 builds 16-byte {weak object, socket name}
// entries at owner+520; +528 is Num, +52C is Max. 3C90B0 resolves them with
// F823F0 before attachment/scale callbacks. This is not a readiness flag.
struct CallbackAttachmentGuard {
    static constexpr unsigned limit=4096; // Bounded read budget, not a native capacity.
    static constexpr unsigned particle_vtable=0x335DB28; // 8CB8C0, LuxParticleSystemComponent.
    enum Reason : unsigned {none,shape,storage,registry,address,type,scale_method};
    static bool validate(uintptr_t base,uintptr_t owner,unsigned side,SnapshotPreflightTrace& trace){
        using D=SnapshotPreflightDiagnostic;
        auto fail=[&](Reason reason,unsigned line){trace.attachment(side,D::attachment_reason,reason);return trace.reject(line);};
        trace.location(__LINE__);const auto count=trace.read<int>(owner+0x528),capacity=trace.read<int>(owner+0x52C);
        trace.attachment(side,D::attachment_count,static_cast<uint32_t>(count));
        trace.attachment(side,D::attachment_capacity,static_cast<uint32_t>(capacity));
        if(count<0 || capacity<count || static_cast<unsigned>(count)>limit)return fail(shape,__LINE__);
        if(!count)return true;
        trace.location(__LINE__);const auto data=trace.ptr(owner+0x520);
        if(data<0x10000 || data>UINTPTR_MAX-static_cast<uintptr_t>(count)*16)return fail(storage,__LINE__);
        unsigned live{},expired{};
        for(int i=0;i<count;++i){
            const auto entry=data+static_cast<uintptr_t>(i)*16;
            trace.attachment(side,D::attachment_entry,entry);trace.attachment(side,D::attachment_object,0);trace.attachment(side,D::attachment_vtable,0);
            trace.location(__LINE__);const auto index=trace.read<int>(entry),serial=trace.read<int>(entry+4);
            // The remaining eight bytes are an FName, not an object pointer.
            trace.location(__LINE__);(void)trace.read<uint64_t>(entry+8);
            uintptr_t object{};
            if(serial && index>=0){
                trace.location(__LINE__);const auto objects=trace.read<int>(base+0x42A115C);
                if(objects<0)return fail(registry,__LINE__);
                if(index<objects){
                    trace.location(__LINE__);const auto table=trace.ptr(base+0x42A1150);
                    if(table<0x10000 || table>UINTPTR_MAX-static_cast<uintptr_t>(index)*24-24)return fail(registry,__LINE__);
                    const auto item=table+static_cast<uintptr_t>(index)*24;
                    trace.location(__LINE__);if(trace.read<int>(item+16)==serial){
                        trace.location(__LINE__);if(!(trace.read<unsigned>(item+8)&0x30000000u))object=trace.ptr(item);
                    }
                }
            }
            // Match F823F0: null/expired/deleted weak references invoke no
            // callback. Never resurrect them or dereference their old object.
            if(!object){trace.attachment(side,D::attachment_expired,++expired);continue;}
            trace.attachment(side,D::attachment_object,object);
            if(object<0x10000)return fail(address,__LINE__);
            trace.location(__LINE__);const auto vtable=trace.ptr(object);trace.attachment(side,D::attachment_vtable,vtable);
            if(vtable!=base+particle_vtable)return fail(type,__LINE__);
            trace.location(__LINE__);if(!trace.method(object,0x3A8,base,0x1DACA60))return fail(scale_method,__LINE__);
            trace.attachment(side,D::attachment_live,++live);
        }
        return true;
    }
};
}
