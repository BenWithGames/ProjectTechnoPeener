#include <initializer_list>
#include "../src/AnimationListGuard.hpp"
#include <cassert>
#include <cstdio>
using G=sc6::research::AnimationListGuard;
int main(){
 for(unsigned length: {1u,64u,65u,128u,768u,4096u}){
  G g;for(unsigned i=1;i<=length;++i)assert(g.visit(i)==G::Result::accepted);
  assert(g.count==length);
 }
 G budget;for(unsigned i=1;i<=G::limit;++i)assert(budget.visit(i)==G::Result::accepted);
 assert(budget.visit(G::limit+1)==G::Result::budget);
 for(unsigned prefix=0;prefix<80;++prefix)for(unsigned cycle=1;cycle<80;++cycle){
  G g;unsigned node=1;bool detected=false;
  for(unsigned n=0;n<G::limit;++n){auto result=g.visit(node);if(result!=G::Result::accepted){assert(result==G::Result::cycle);detected=true;break;}
   node=node==prefix+cycle?prefix+1:node+1;
  }assert(detected);
 }
 G fresh;assert(fresh.visit(1)==G::Result::accepted);
 std::puts("Animation list guard PASS: long finite lists, budget, self/multi-node cycles and independent lists");
}

