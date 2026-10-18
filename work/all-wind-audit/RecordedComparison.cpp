#include "../../src/EffectLifecycleWindow.hpp"
#include <fstream>
#include <cassert>
#include <cstdio>
int main(){using L=sc6::research::EffectLifecycleWindow<64,16>;
for(const char* path:{"build-native/wind-gap-61168.bin","build-native/wind-gap-46908.bin","build-native/wind-gap-61160.bin","build-native/wind-gap-47324.bin"}){
L::Image a{},b{};std::ifstream f(path,std::ios::binary);f.read(reinterpret_cast<char*>(&a),sizeof(a));f.read(reinterpret_cast<char*>(&b),sizeof(b));assert(f);
unsigned object{},offset{};assert(L::equivalent(a,b,object,offset));
b.objects[0].bytes[0x148]^=std::byte{1};assert(!L::equivalent(a,b,object,offset)&&offset==0x148);
std::printf("PASS recorded evidence and active-field rejection: %s\n",path);
}}