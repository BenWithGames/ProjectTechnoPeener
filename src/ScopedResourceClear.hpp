#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
namespace sc6::research {
// Layout verified against 899110, FF0C80, 1C14E80 and 716430.
// Inspect ONLY while holding container+208's native critical section.
struct ScopedResourceClear {
    enum Result:unsigned {unknown,unrelated,related};
    enum Reason:unsigned {none,context,layout,read_fault,unsupported_resource,secondary_entries,dependency_match};
    struct Dependency {uintptr_t object{},control{},payload{},size{};};
    struct Evidence {unsigned result{},reason{},visited{},secondary{},index{UINT32_MAX};uintptr_t matched{},container{},caller{},secondary_buffer{},secondary_bytes{};unsigned secondary_validated{},secondary_length{},secondary_capacity{},secondary_check{};};
    template<class T> static T read(uintptr_t p){T v;std::memcpy(&v,reinterpret_cast<const void*>(p),sizeof(v));return v;}
    static bool range(uintptr_t p,uintptr_t size){return p>=0x10000 && size && p<=UINTPTR_MAX-size;}
    static bool overlap(uintptr_t a,uintptr_t an,uintptr_t b,uintptr_t bn){return range(a,an)&&range(b,bn)&&a<b+bn&&b<a+an;}
    static bool matches(uintptr_t object,uintptr_t control,uintptr_t payload,uintptr_t size,const Dependency* deps,unsigned count){
        for(unsigned i=0;i<count;++i){const auto& d=deps[i];
            if((object && object==d.object)||(control && control==d.control)||overlap(payload,size,d.payload,d.size))return true;
        }return false;
    }
    static bool readable(uintptr_t p,uintptr_t size){
        if(!range(p,size))return false;
        const auto end=p+size;
        while(p<end){MEMORY_BASIC_INFORMATION m{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m)) || m.State!=MEM_COMMIT ||
               (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
               !(m.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
            const auto start=reinterpret_cast<uintptr_t>(m.BaseAddress);
            if(!range(start,m.RegionSize) || start+m.RegionSize<=p)return false;
            p=start+m.RegionSize;
        }return true;
    }
    static bool header(uintptr_t collection,uintptr_t& data,uintptr_t& bits,unsigned& count){
        data=read<uintptr_t>(collection);count=read<unsigned>(collection+8);
        const auto capacity=read<unsigned>(collection+12),nbits=read<unsigned>(collection+0x28),bitcap=read<unsigned>(collection+0x2c);
        bits=read<uintptr_t>(collection+0x20);if(!bits)bits=collection+0x10;
        return count<=4096 && capacity>=count && capacity<=1048576 && nbits==count && bitcap>=nbits && bitcap<=1048576 &&
            (bits!=collection+0x10 || bitcap<=128) && (!count || (range(data,uintptr_t(count)*32)&&range(bits,((count+31)/32)*4)));
    }
    static Evidence inspect(uintptr_t container,uintptr_t caller,uintptr_t module,const Dependency* deps,unsigned count,bool locked){
        Evidence e{};e.container=container;e.caller=caller;e.reason=context;
        if(!locked || !module || !range(container,0x230) || !deps || !count || count>3)return e;
        __try {
            for(unsigned i=0;i<count;++i)if(!range(deps[i].control,24)||!range(deps[i].payload,deps[i].size))return e;
            uintptr_t data{},bits{};unsigned n{};
            e.reason=layout;if(!header(container+0x100,data,bits,n))return e;
            for(unsigned i=0;i<n;++i){
                if(!(read<unsigned>(bits+(i/32)*4)&(1u<<(i%32))))continue;
                ++e.visited;e.index=i;
                const auto entry=data+uintptr_t(i)*32,object=read<uintptr_t>(entry+8),control=read<uintptr_t>(entry+16);
                if(matches(object,control,0,0,deps,count)){e.result=related;e.reason=dependency_match;e.matched=control?control:object;return e;}
                if(!control && !object)continue;
                // Unknown destructor types may release dependencies indirectly. Fail closed.
                e.reason=unsupported_resource;
                if(!range(control,24)||!range(object,0x51)||read<uintptr_t>(control)!=module+0x36E9D40 ||
                    read<uintptr_t>(control+16)!=object || read<int>(control+8)<=0 || read<int>(control+12)<=0 ||
                    read<uintptr_t>(object)!=module+0x3CEA0F8)return e;
                const auto payload=read<uintptr_t>(object+0x38);const auto size=read<int>(object+0x48);
                if(size<=0 || !range(payload,static_cast<uintptr_t>(size)))return e;
                if(matches(object,control,payload,static_cast<uintptr_t>(size),deps,count)){e.result=related;e.reason=dependency_match;e.matched=payload;return e;}
            }
            e.reason=layout;if(!header(container+0x150,data,bits,n))return e;
            // 85B76D deep-copies the UTF-16 path at entry+8 via 409440.
            // 716478 frees only that buffer; it does not destroy the resolved asset.
            // Validate the pinned string layout and full allocation range before ignoring it.
            for(unsigned i=0;i<n;++i)if(read<unsigned>(bits+(i/32)*4)&(1u<<(i%32))){
                ++e.secondary;e.index=i;e.reason=secondary_entries;e.secondary_check=1;
                const auto entry=data+uintptr_t(i)*32;
                const auto buffer=read<uintptr_t>(entry+8);
                const auto length=read<int>(entry+16),capacity=read<int>(entry+20);
                e.secondary_buffer=buffer;e.secondary_length=static_cast<unsigned>(length);
                e.secondary_capacity=static_cast<unsigned>(capacity);e.secondary_bytes=0;
                if(length<0 || capacity<length || capacity>1048576)return e;
                if(!buffer){if(length || capacity)return e;++e.secondary_validated;e.secondary_check=0;continue;}
                if(!capacity || (buffer&1))return e;
                const auto bytes=uintptr_t(capacity)*2;e.secondary_bytes=bytes;
                if(!range(buffer,bytes))return e;
                e.secondary_check=2;
                for(unsigned d=0;d<count;++d){const auto& dep=deps[d];
                    if(overlap(buffer,bytes,dep.payload,dep.size) ||
                       overlap(buffer,bytes,dep.control,24) ||
                       (dep.object && overlap(buffer,bytes,dep.object,0x51))){
                        e.result=related;e.reason=dependency_match;e.matched=buffer;return e;
                    }
                }
                e.secondary_check=3;
                // A nonempty native string includes its terminating UTF-16 zero.
                if(!readable(buffer,bytes))return e;
                if(length && read<uint16_t>(buffer+uintptr_t(length-1)*2)!=0)return e;
                ++e.secondary_validated;e.secondary_check=0;
            }
            e.result=unrelated;e.reason=none;e.index=UINT32_MAX;return e;
        } __except(EXCEPTION_EXECUTE_HANDLER){e.result=unknown;e.reason=read_fault;return e;}
    }
};
}
