#include "../src/NetworkInputComposer.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
int main(int argc,char** argv){
    using C=sc6::NetworkInputComposer;
    C::Envelope local{1,2,3,4,5,6,7,8,9,10};
    C::Envelope peer{11,12,13,14,15,16,17,18,19,20};
    for(unsigned slot=1;slot<=2;++slot){
        auto result=C::compose(local,peer,slot,42,42);assert(result);
        for(unsigned i=0;i<10;++i){
            const bool remote=i==(slot-1)*2 || i==(slot-1)*2+1;
            assert((*result)[i]==(remote?peer[i]:local[i]));
        }
    }
    assert(!C::compose(local,peer,0,42,42));
    assert(!C::compose(local,peer,3,42,42));
    assert(!C::compose(local,peer,1,42,43));
    auto release=peer;release[0]=release[1]=0;
    auto result=C::compose(local,release,1,43,43);
    assert(result && (*result)[0]==0 && (*result)[1]==0 && (*result)[4]==5);
    assert(local[0]==1 && peer[0]==11);
    if(argc==2){
        std::ifstream in(argv[1]);assert(in);
        unsigned count=0;uint32_t frame{},peer_frame{};unsigned slot{};
        while(in>>frame){
            assert(in>>peer_frame>>slot);
            C::Envelope observed{},received{};
            for(auto& word:observed)assert(in>>word);
            for(auto& word:received)assert(in>>word);
            auto composed=C::compose(observed,received,slot,frame,peer_frame);
            assert(composed && *composed==observed);
            // Corrupt ONLY the candidate remote pair, then correct it with the
            // received pair. Compare all ten words against independent capture.
            auto wrong=observed;wrong[(slot-1)*2]^=1;wrong[(slot-1)*2+1]^=1;
            auto corrected=C::compose(wrong,received,slot,frame,peer_frame);
            assert(corrected && *corrected==observed);++count;
        }
        assert(in.eof() && count>0);
        std::cout<<"PASS: "<<count<<" recorded compositions and input-buffer corrections; no game updates invoked\n";
    }
    std::cout<<"PASS: both slots, preserve local auxiliary bytes, exact release, frame/slot rejection, immutable sources\n";
}
