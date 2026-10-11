#pragma once
#include "ResourceReferenceLease.hpp"
namespace sc6::research {
class GeneratedSnapshotDependency {
 ResourceReferenceLease lease_;DWORD owner_{};
 ResourceReferenceLease::Dispose object_{},dispose_{};
public:
 uintptr_t root{},buffer{},control{},size{};
 bool active()const{return lease_.active();}
 bool acquire(uintptr_t query,uintptr_t bytes,uintptr_t owner,uintptr_t length,ResourceReferenceLease::Dispose object,ResourceReferenceLease::Dispose dispose){
  if(active())return root==query && buffer==bytes && control==owner && size==length;
  if(!query || !bytes || !length || !lease_.acquire(owner,GetCurrentThreadId(),0,object,dispose))return false;
  root=query;buffer=bytes;control=owner;size=length;owner_=GetCurrentThreadId();object_=object;dispose_=dispose;return true;
 }
 template<class Pool,class Handle> bool bind(Pool& pool,const Handle& handle){
  return active() && owner_==GetCurrentThreadId() && pool.retain_dependency(handle,control,object_,dispose_);
 }
 bool reset(){return !active() || lease_.release(0,true)!=ResourceReferenceLease::Release::rejected;}
};
}
