#include "../src/LiveShadowPrediction.hpp"
#include <cassert>
#include <memory>
#include <iostream>
struct Fixture {
    std::array<sc6::observation::Record,sc6::observation::capacity> local{},remote{};
    std::array<bool,sc6::observation::capacity> seen{};
    std::array<uint64_t,sc6::observation::capacity> arrivals{};
    void setup(unsigned late){for(unsigned i=0;i<40;++i){local[i].data[0]=100+i;local[i].data[3]=i+1;local[i].data[11]=10000+i*1000;remote[i]=local[i];seen[i]=true;arrivals[i]=10000+(i+late)*1000;}}
    void run(sc6::LiveShadowPrediction& s,unsigned published=40){s.update(local,published,remote,seen,arrivals,false);}
};
int main(){
    auto f=std::make_unique<Fixture>();
    f->setup(2);static sc6::LiveShadowPrediction ready;f->run(ready);assert(ready.scheduled==36 && ready.predicted==0 && ready.within_observed_bounds());
    f->setup(6);static sc6::LiveShadowPrediction bounded;f->run(bounded);assert(bounded.wrong>0 && bounded.max_depth==3 && bounded.over_limit==0);
    f->setup(7);static sc6::LiveShadowPrediction exceeded;f->run(exceeded);assert(exceeded.over_limit>0 && exceeded.max_depth==4 && !exceeded.within_observed_bounds());
    auto count=exceeded.wrong;f->run(exceeded);assert(exceeded.wrong==count);
    f->setup(2);f->remote[8].data[3]=999;static sc6::LiveShadowPrediction conflict;f->run(conflict);assert(conflict.truth_mismatches==1);
    f->setup(2);f->local[8].data[0]++;static sc6::LiveShadowPrediction discontinuity;f->run(discontinuity);assert(discontinuity.invalid);
    f->setup(7);static sc6::LiveShadowPrediction incremental;for(unsigned n=1;n<=40;++n)f->run(incremental,n);
    assert(incremental.wrong==exceeded.wrong && incremental.over_limit==exceeded.over_limit);
    std::cout<<"PASS: four-frame schedule, on-time inputs, three-frame limit, over-limit latch, no double-counting, truth mismatch, discontinuity, incremental/final agreement\n";
}
