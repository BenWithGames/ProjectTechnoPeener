#include "../src/SteamObserver.hpp"
#include <cassert>
#include <iostream>
namespace obs=sc6::steam_observer;
static int sends=0,reads=0;
static char payload=42;
static bool fake_send(void* self,uint64_t peer,const void* data,uint32_t bytes,int kind,int channel){
    assert(self==&payload && peer==123 && data==&payload && bytes==1 && kind==2 && channel==7);
    ++sends;return false;
}
static bool fake_read(void* self,void* data,uint32_t bytes,uint32_t* size,uint64_t* peer,int channel){
    assert(self==&payload && data==&payload && bytes==1 && channel==9);
    ++reads;*size=1;*peer=456;*static_cast<char*>(data)=77;return true;
}
int main(){
    obs::observed=&payload;obs::original_send=fake_send;obs::original_read=fake_read;obs::active=true;
    assert(!obs::send(&payload,123,&payload,1,2,7));
    uint32_t size=0;uint64_t peer=0;
    assert(obs::read(&payload,&payload,1,&size,&peer,9));
    assert(sends==1 && reads==1 && size==1 && peer==456 && payload==77);
    assert(obs::used==2 && obs::rows[0].failed==1 && obs::rows[1].bytes==1);
    obs::active=false;
    assert(!obs::send(&payload,123,&payload,1,2,7));assert(obs::rows[0].calls==1 && sends==2);
    obs::active=true;
    obs::record(nullptr,1,0,false,1,true);assert(obs::used==2);
    for(unsigned i=0;i<200;++i)obs::record(&payload,1000+i,0,false,1,true);
    assert(obs::used==128 && obs::dropped==74);
    std::cout<<"PASS: original arguments, one original call, return/output preservation, inactive/foreign interface filtering, bounded metadata overflow\n";
}
