#include "../src/ScopedResourceClear.hpp"
#include "../src/ResourceClearObserver.hpp"
#include <cassert>
#include "../src/MatchEndObservation.hpp"
#include <thread>
#include <iostream>
using namespace sc6::research;
template<class T> void put(void* p,size_t off,T value){std::memcpy(static_cast<char*>(p)+off,&value,sizeof(value));}
static unsigned calls;static bool notified;
static uintptr_t native_clear(uintptr_t p){
 bool acquired=false;std::thread competing([&]{acquired=TryEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(p+0x208))!=0;if(acquired)LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(p+0x208));});competing.join();assert(!acquired);
 assert(notified);assert(TryEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(p+0x208)));
 LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(p+0x208));++calls;SetLastError(123);return 77;
}
static void notify(uintptr_t,uintptr_t caller,bool locked){assert(caller==42 && locked);notified=true;}
static void unavailable(uintptr_t,uintptr_t,bool locked){assert(!locked);notified=true;}
static uintptr_t no_lock_native(uintptr_t){assert(notified);++calls;return 88;}
static uintptr_t throwing_native(uintptr_t){RaiseException(0xE0123456,0,0,nullptr);return 0;}
static bool exception_released(uintptr_t p){
 __try {resource_clear::observe(p,throwing_native,42);return false;}
 __except(GetExceptionCode()==0xE0123456?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
int main(){
 MatchEndObservation end{0x3E87428,0x2D2BC0,42,0x301590};
 assert(end.cleanup_allowed(42,42,false,true,0));
 assert(end.cleanup_allowed(42,42,false,true,7));
 assert(!end.cleanup_allowed(0,42,false,true,7));
 assert(!end.cleanup_allowed(42,41,false,true,7));
 assert(!end.cleanup_allowed(42,42,true,true,7));
 assert(!end.cleanup_allowed(42,42,false,false,7));
 for(unsigned reason: {1u,2u,3u,4u,5u,6u,8u,9u,10u,UINT_MAX})assert(!end.cleanup_allowed(42,42,false,true,reason));
 for(unsigned vt: {0u,0x3E87378u,0x3E873D0u,0x3E87480u,0x3E87530u}){
  auto other=end;other.vtable=vt;assert(!other.cleanup_allowed(42,42,false,true,7));
 }
 auto bad_query=end;bad_query.state_query++;assert(!bad_query.cleanup_allowed(42,42,false,true,7));
 auto wrong=end;wrong.handler++;assert(!wrong.cleanup_allowed(42,42,false,true,7));
 assert(!MatchEndObservation::read(0).confirmed(42));
 assert(!MatchEndObservation::read(1).confirmed(42));

 constexpr uintptr_t base=0x140000000;
 alignas(16) std::array<std::byte,0x240> container{};
 std::array<std::byte,96> entries{};std::array<std::byte,0xc0> object{};std::array<std::byte,24> control{};
 std::array<std::byte,64> payload{},retained{};std::array<std::byte,24> retained_control{};
 auto c=reinterpret_cast<uintptr_t>(container.data());
 ScopedResourceClear::Dependency dep{0,reinterpret_cast<uintptr_t>(retained_control.data()),reinterpret_cast<uintptr_t>(retained.data()),retained.size()};
 auto inspect=[&](bool locked=true){return ScopedResourceClear::inspect(c,42,base,&dep,1,locked);};
 assert(inspect().result==ScopedResourceClear::unrelated);
 assert(inspect(false).result==ScopedResourceClear::unknown);
 assert(ScopedResourceClear::inspect(c,0,base,&dep,0,true).result==ScopedResourceClear::unknown);
 const auto saved_size=dep.size;dep.size=UINTPTR_MAX;assert(inspect().result==ScopedResourceClear::unknown);dep.size=saved_size;
 assert(ScopedResourceClear::inspect(1,0,base,&dep,1,true).result==ScopedResourceClear::unknown);
 put(container.data(),0x100,reinterpret_cast<uintptr_t>(entries.data()));put(container.data(),0x108,3u);put(container.data(),0x10c,3u);
 put(container.data(),0x128,3u);put(container.data(),0x12c,32u);put(container.data(),0x110,4u); // allocated slot2 only
 put(entries.data(),8,uintptr_t(1)); // free-list garbage must not be dereferenced
 put(entries.data(),64+8,reinterpret_cast<uintptr_t>(object.data()));put(entries.data(),64+16,reinterpret_cast<uintptr_t>(control.data()));
 put(object.data(),0,base+0x3CEA0F8);put(object.data(),0x38,reinterpret_cast<uintptr_t>(payload.data()));put(object.data(),0x48,64);
 put(control.data(),0,base+0x36E9D40);put(control.data(),8,2);put(control.data(),12,1);put(control.data(),16,reinterpret_cast<uintptr_t>(object.data()));
 auto e=inspect();assert(e.result==ScopedResourceClear::unrelated && e.visited==1);
 put(object.data(),0x38,dep.payload+8);assert(inspect().result==ScopedResourceClear::related);
 put(object.data(),0x38,reinterpret_cast<uintptr_t>(payload.data()));put(entries.data(),80,dep.control);assert(inspect().result==ScopedResourceClear::related);
 put(entries.data(),80,reinterpret_cast<uintptr_t>(control.data()));put(object.data(),0,uintptr_t(0));assert(inspect().reason==ScopedResourceClear::unsupported_resource);
 put(object.data(),0,base+0x3CEA0F8);put(container.data(),0x128,4u);assert(inspect().reason==ScopedResourceClear::layout);put(container.data(),0x128,3u);
 put(container.data(),0x108,4097u);assert(inspect().reason==ScopedResourceClear::layout);put(container.data(),0x108,3u);
 put(entries.data(),80,uintptr_t(0x10000));assert(inspect().reason==ScopedResourceClear::read_fault);put(entries.data(),80,reinterpret_cast<uintptr_t>(control.data()));
 // Heap bit storage, including allocated slot2; no inline assumption.
 unsigned bits=4;put(container.data(),0x120,reinterpret_cast<uintptr_t>(&bits));assert(inspect().result==ScopedResourceClear::unrelated);
 put(container.data(),0x150,reinterpret_cast<uintptr_t>(entries.data()));put(container.data(),0x158,3u);put(container.data(),0x15c,3u);
 put(container.data(),0x178,3u);put(container.data(),0x17c,32u);put(container.data(),0x160,4u);
 assert(inspect().reason==ScopedResourceClear::secondary_entries);
 // Secondary storage is a copied UTF-16 path, not the resolved resource.
 std::array<uint16_t,16> path{u'/',u'G',u'a',u'm',u'e',0};
 const auto path_address=reinterpret_cast<uintptr_t>(path.data());
 put(entries.data(),72,path_address);put(entries.data(),80,6);put(entries.data(),84,16);
 // Keep the primary collection empty so its different entry layout is independent.
 put(container.data(),0x110,0u);bits=0;
 e=inspect();assert(e.result==ScopedResourceClear::unrelated && e.secondary_validated==1 && e.secondary_bytes==32);
 assert(path[0]==u'/' && path[5]==0); // inspection is read-only
 put(entries.data(),80,-1);assert(inspect().reason==ScopedResourceClear::secondary_entries);put(entries.data(),80,6);
 put(entries.data(),84,5);assert(inspect().result==ScopedResourceClear::unknown);put(entries.data(),84,16);
 path[5]=1;assert(inspect().secondary_check==3 && inspect().result==ScopedResourceClear::unknown);path[5]=0;
 put(entries.data(),72,path_address+1);assert(inspect().result==ScopedResourceClear::unknown);
 put(entries.data(),72,dep.payload);assert(inspect().result==ScopedResourceClear::related);
 put(entries.data(),72,dep.control+2);assert(inspect().result==ScopedResourceClear::related);
 dep.object=path_address+16;put(entries.data(),72,path_address);assert(inspect().result==ScopedResourceClear::related);dep.object=0;
 put(entries.data(),72,uintptr_t(0x10000));assert(inspect().result==ScopedResourceClear::unknown);
 put(entries.data(),72,uintptr_t(0));assert(inspect().result==ScopedResourceClear::unknown);
 put(entries.data(),80,0);put(entries.data(),84,0);assert(inspect().result==ScopedResourceClear::unrelated);
 put(entries.data(),72,path_address);assert(inspect().result==ScopedResourceClear::unknown);
 put(entries.data(),84,16);assert(inspect().result==ScopedResourceClear::unrelated); // allocated empty string
 put(entries.data(),84,1048577);assert(inspect().result==ScopedResourceClear::unknown);
 put(entries.data(),72,UINTPTR_MAX-1);put(entries.data(),84,16);assert(inspect().result==ScopedResourceClear::unknown);
 put(container.data(),0x160,0u);assert(inspect().result==ScopedResourceClear::unrelated); // free slots ignored
 // Observer calls original exactly once under the same recursive lock, preserving return/error.
 InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208));
 dependency_events::scoped_clear=notify;
 assert(resource_clear::observe(c,native_clear,42)==77 && calls==1 && GetLastError()==123);
 assert(exception_released(c)); // added recursion unwound even when native raises
 std::atomic<bool> held{},release{};
 std::thread holder([&]{EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208));held=true;while(!release.load())std::this_thread::yield();LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208));});
 while(!held.load())std::this_thread::yield();
 notified=false;dependency_events::scoped_clear=unavailable;
 assert(resource_clear::observe(c,no_lock_native,42)==88 && calls==2);
 release=true;holder.join();
 dependency_events::scoped_clear=nullptr;
 bool other_acquired=false;std::thread t([&]{other_acquired=TryEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208))!=0;if(other_acquired)LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208));});t.join();assert(other_acquired);
 DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(c+0x208));
 std::cout<<"Scoped resource clear tests passed\n";
}
