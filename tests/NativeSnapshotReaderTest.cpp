#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "../src/FullRestoreStream.hpp"
#include <memory>
#include <iostream>
#include <cstdlib>
using Stream=sc6::research::FullRestoreStream;
void require(bool value){if(!value){std::cerr<<"Native snapshot reader test failed\n";std::exit(1);}}
bool invalid_read(Stream& stream){
    unsigned out{};
    __try{stream.stream.slots->read(&stream.stream,&out,3);}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
    return false;
}
int main(){
    auto snapshot=std::make_unique<Stream>();unsigned first=123;std::uint64_t second=456;
    snapshot->stream.slots->write(&snapshot->stream,&first,sizeof(first));
    snapshot->stream.slots->write(&snapshot->stream,&second,sizeof(second));require(snapshot->seal());
    const auto payload=snapshot->stream.payload;
    for(unsigned n=0;n<20;++n){
        auto reader=std::make_unique<Stream>();require(reader->prepare_reader_from(*snapshot));
        unsigned a{};std::uint64_t b{};
        reader->stream.slots->read(&reader->stream,&a,sizeof(a));
        reader->stream.slots->read(&reader->stream,&b,sizeof(b));
        require(a==first && b==second && reader->consumed());
        require(!reader->prepare_reader_from(*snapshot));
        require(snapshot->reads==0 && snapshot->stream.read_position==0 && snapshot->stream.payload==payload);
    }
    auto failed=std::make_unique<Stream>();require(failed->prepare_reader_from(*snapshot));
    require(invalid_read(*failed) && failed->stream.failures);
    auto fresh=std::make_unique<Stream>();require(!fresh->prepare_reader_from(*failed));
    require(fresh->writes==0 && !fresh->sealed && fresh->stream.write_position==0);
    require(fresh->prepare_reader_from(*snapshot));
    for(unsigned mode=0;mode<8;++mode){
        auto bad=std::make_unique<Stream>(*snapshot),destination=std::make_unique<Stream>();
        if(mode==0)bad->sealed=false;
        if(mode==1)bad->lengths[0]=0;
        if(mode==2)++bad->lengths[1];
        if(mode==3)--bad->lengths[1];
        if(mode==4)bad->stream.read_position=1;
        if(mode==5)bad->reads=1;
        if(mode==6)bad->writes=Stream::transfer_limit+1;
        if(mode==7)bad->stream.write_position=sc6::research::SaveStream::limit+1;
        require(!destination->prepare_reader_from(*bad));
        require(!destination->sealed && !destination->writes);
    }
    require(!snapshot->prepare_reader_from(*snapshot));
    std::cout<<"PASS: 20 independent native readers; immutable capture; failed-reader isolation; malformed, consumed, self-copy and reuse rejection\n";
}
