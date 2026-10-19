#include "../src/NativePredictionFrontier.hpp"
#include <cassert>
#include <sstream>
using F=sc6::research::NativePredictionFrontier;
int main(){for(unsigned mask:{1u,2u})for(unsigned frame:{0u,511u,512u,1000u}){
 F::Sample s;s.identity=7;s.frame=frame;s.local_cursor=frame+4;s.all_cursor=frame;s.local_mask=mask;s.phase=2;
 unsigned l=mask==1?0:1;s.entries[l]={7,frame,0,1};assert(F::classify(s)==F::remote_missing); // neutral is valid
 assert(F::eligible(s,2,true,true));assert(F::eligible(s,0,true,true));
 assert(!F::eligible(s,3,true,true));assert(!F::eligible(s,1,true,true));assert(!F::eligible(s,4,true,true));
 assert(!F::eligible(s,2,true,false));
 s.entries[1-l]={7,frame,0,1};assert(F::classify(s)==F::confirmed);
 s.entries[1-l].identity=6;assert(F::classify(s)==F::remote_missing);
 s.entries[1-l]={7,frame+512,1,1};assert(F::classify(s)==F::remote_missing); // ring alias cannot confirm
 s.entries[l].valid=0;assert(F::classify(s)==F::local_missing);s.entries[l].valid=1;
 for(unsigned reason:{1u,4u,5u}){s.wait_reason=reason;assert(F::classify(s)==F::transition);}s.wait_reason=0;
 if(frame>=3){s.all_cursor=frame-3;assert(F::classify(s)==F::budget);s.all_cursor=frame-2;assert(F::classify(s)==F::remote_missing);}
 s.phase=5;assert(F::classify(s)==F::transition);
 }
 F::Sample s;assert(!F::read(1,1,s));F::Observation o;std::ostringstream out;o.report(out);assert(out.str().find("prediction_wait_bypass_enabled=0")!=std::string::npos);
}
