#pragma once
#include <Windows.h>
#include "FullRestoreStream.hpp"
#include <span>
#include <map>
#include <string>
#include <memory>
namespace sc6::research {
// Offline import only: archived source addresses are evidence, never destinations.
// This reconstructs serializer storage, NOT live objects or ownership bindings.
struct ArchivedSnapshotStream {
    static constexpr bool native_restore_authorized=false;
    static bool import(std::span<const std::byte> file, FullRestoreStream& destination){
        if(file.size()<8 || file.size()>32*1024*1024)return false;
        auto u64=[](const std::byte* p){uint64_t v{};std::memcpy(&v,p,8);return v;};
        if(u64(file.data())!=0x3144564543364353ULL)return false;
        std::map<std::string,std::span<const std::byte>> sections;
        size_t cursor=8;
        while(cursor<file.size()){
            if(file.size()-cursor<16)return false;
            const auto n=u64(file.data()+cursor),size=u64(file.data()+cursor+8);cursor+=16;
            if(!n || n>128 || n>file.size()-cursor)return false;
            std::string name(reinterpret_cast<const char*>(file.data()+cursor),static_cast<size_t>(n));cursor+=static_cast<size_t>(n);
            if(size>file.size()-cursor || sections.contains(name))return false;
            sections.emplace(name,file.subspan(cursor,static_cast<size_t>(size)));cursor+=static_cast<size_t>(size);
        }
        auto exact=[&](const char* name,size_t size){auto it=sections.find(name);return it!=sections.end() && it->second.size()==size;};
        if(!exact("capture_valid",1) || sections.at("capture_valid")[0]!=std::byte{1} ||
           !exact("capture_stage",4) || !exact("exception",4))return false;
        unsigned stage{},exception{};
        std::memcpy(&stage,sections.at("capture_stage").data(),4);std::memcpy(&exception,sections.at("exception").data(),4);
        if(stage!=15 || exception)return false;
        auto snapshot=std::make_unique<FullRestoreStream>();
        if(!exact("native.saved.lengths",sizeof(snapshot->lengths)) || !exact("native.saved.traces",sizeof(snapshot->traces)) ||
           !exact("native.saved.size",8) || !exact("native.saved.payload",SaveStream::limit))return false;
        std::memcpy(snapshot->lengths.data(),sections.at("native.saved.lengths").data(),sizeof(snapshot->lengths));
        std::memcpy(snapshot->traces.data(),sections.at("native.saved.traces").data(),sizeof(snapshot->traces));
        const auto size=u64(sections.at("native.saved.size").data());
        if(!size || size>SaveStream::limit)return false;
        uint64_t total=0;bool ended=false;
        for(auto length:snapshot->lengths){
            if(!length){ended=true;continue;}
            if(ended || length>size-total)return false;
            total+=length;++snapshot->writes;
        }
        if(total!=size)return false;
        for(unsigned i=0;i<snapshot->writes;++i){
            // Captures must not already contain reader-side state.
            if(snapshot->traces[i].destination || snapshot->traces[i].reader)return false;
        }
        std::memcpy(snapshot->stream.payload.data(),sections.at("native.saved.payload").data(),static_cast<size_t>(size));
        snapshot->stream.write_position=size;
        if(!snapshot->seal())return false;
        return destination.prepare_reader_from(*snapshot);
    }
};
}
