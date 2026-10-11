#pragma once
#include "ResourceReferenceLease.hpp"
namespace sc6::research {
class GeneratedBufferHold {
 ResourceReferenceLease lease_;DWORD owner_{};unsigned depth_{};
public:
 unsigned acquired{},released{},cancelled{},rejected{},cancel_reason{};
 unsigned depth()const{return depth_;}
 bool pending()const{return lease_.active();}
 bool capture(uintptr_t control,DWORD known_owner,ResourceReferenceLease::Dispose object,ResourceReferenceLease::Dispose dispose){
  if(acquired || !known_owner || known_owner!=GetCurrentThreadId()){++rejected;return false;}
  if(!lease_.acquire(control,known_owner,0,object,dispose)){++rejected;return false;}
  owner_=known_owner;depth_=0;++acquired;return true;
 }
 void before_setup(uintptr_t held_root,uintptr_t setup_root){if(held_root && held_root==setup_root)step(true,1);}
 void step(bool cancel=false,unsigned reason=0){
  if(!pending() || owner_!=GetCurrentThreadId())return;
  if(!cancel)++depth_;
  if(lease_.release(depth_,cancel)!=ResourceReferenceLease::Release::rejected){++released;if(cancel){++cancelled;cancel_reason=reason;}}
 }
};
}
