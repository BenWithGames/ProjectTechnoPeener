#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/ArchivedSnapshotStream.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
using namespace sc6::research;
void require(bool ok){if(!ok){std::cerr<<"FAIL archived stream\n";std::exit(1);}}
int main(int argc,char** argv){
    require(argc>1);
    static_assert(!ArchivedSnapshotStream::native_restore_authorized);
    for(int i=1;i<argc;++i){
        std::ifstream in(argv[i],std::ios::binary|std::ios::ate);require(bool(in));
        auto size=in.tellg();require(size>0 && size<=32*1024*1024);in.seekg(0);
        std::vector<std::byte> bytes(static_cast<size_t>(size));in.read(reinterpret_cast<char*>(bytes.data()),size);require(bool(in));
        const auto original=bytes;
        auto archive=std::make_unique<FullRestoreStream>();require(ArchivedSnapshotStream::import(bytes,*archive));
        for(unsigned cycle=0;cycle<3;++cycle){
            auto reader=std::make_unique<FullRestoreStream>();require(reader->prepare_reader_from(*archive));
            std::vector<std::byte> restored(static_cast<size_t>(archive->stream.write_position));size_t at=0;
            for(unsigned n=0;n<reader->writes;++n){const auto length=reader->lengths[n];reader->stream.slots->read(&reader->stream,restored.data()+at,length);at+=static_cast<size_t>(length);}
            require(reader->consumed() && !std::memcmp(restored.data(),archive->stream.payload.data(),restored.size()));
        }
        require(bytes==original && archive->reads==0);
        require(!ArchivedSnapshotStream::import(bytes,*archive));
        auto corrupt_section=[&](const char* target,size_t offset,std::byte value){
            auto corrupt=original;size_t cursor=8;bool found=false;
            while(cursor<corrupt.size()){
                uint64_t n{},length{};std::memcpy(&n,corrupt.data()+cursor,8);std::memcpy(&length,corrupt.data()+cursor+8,8);cursor+=16;
                std::string name(reinterpret_cast<const char*>(corrupt.data()+cursor),static_cast<size_t>(n));cursor+=static_cast<size_t>(n);
                if(name==target){require(offset<length);corrupt[cursor+offset]=value;found=true;break;}cursor+=static_cast<size_t>(length);
            }
            require(found);auto rejected=std::make_unique<FullRestoreStream>();
            require(!ArchivedSnapshotStream::import(corrupt,*rejected));require(!rejected->sealed && !rejected->writes);
        };
        corrupt_section("capture_valid",0,std::byte{0});
        corrupt_section("capture_stage",0,std::byte{14});
        corrupt_section("exception",0,std::byte{1});
        corrupt_section("native.saved.size",7,std::byte{127});
        corrupt_section("native.saved.lengths",7,std::byte{127});
        corrupt_section("native.saved.lengths",4095*8,std::byte{1});
        corrupt_section("native.saved.traces",8,std::byte{1});
        for(auto length:{size_t(0),size_t(7),bytes.size()-1}){
            auto rejected=std::make_unique<FullRestoreStream>();require(!ArchivedSnapshotStream::import(std::span(bytes).first(length),*rejected));require(!rejected->sealed && !rejected->writes);
        }
        bytes[0]=std::byte{};auto bad=std::make_unique<FullRestoreStream>();require(!ArchivedSnapshotStream::import(bytes,*bad));
        std::cout<<"PASS "<<argv[i]<<" records="<<archive->writes<<" bytes="<<archive->stream.write_position<<" independent_reads=3 native_restore=0\n";
    }
}
