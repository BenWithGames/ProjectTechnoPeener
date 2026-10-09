// Standalone diagnostic: synthetic inputs and state, never attaches to SC6.
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "../src/OfflineRollbackSession.hpp"

namespace {
using Clock=std::chrono::steady_clock;
using Bytes=std::array<unsigned char,96>;
constexpr unsigned frames=120,protocol=1,schema=1,simulation=1;
enum Kind:unsigned {hello=1,ready=2,input=3,ping=4,pong=5,result=6};
struct Packet {unsigned kind{},role{},frame{},value{},sequence{};std::uint64_t nonce{},echo{};};
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
void put(Bytes& b,unsigned n,unsigned value){for(unsigned i=0;i<4;++i)b[n*4+i]=static_cast<unsigned char>(value>>(24-i*8));}
unsigned get(const Bytes& b,unsigned n){unsigned v=0;for(unsigned i=0;i<4;++i)v=(v<<8)|b[n*4+i];return v;}
struct Crypto {
    BCRYPT_ALG_HANDLE alg{};std::array<unsigned char,32> key{};
    explicit Crypto(const std::string& path){
        std::ifstream file(path);std::string hex,extra;file>>hex;
        require(hex.size()==64 && !(file>>extra),"Key file must contain exactly 64 hexadecimal characters");
        auto digit=[](char c)->unsigned {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;throw std::runtime_error("Invalid key");};
        for(unsigned i=0;i<32;++i)key[i]=static_cast<unsigned char>(digit(hex[i*2])*16+digit(hex[i*2+1]));
        require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0,"HMAC provider failed");
    }
    ~Crypto(){if(alg)BCryptCloseAlgorithmProvider(alg,0);SecureZeroMemory(key.data(),key.size());}
    std::array<unsigned char,32> mac(const Bytes& bytes){
        BCRYPT_HASH_HANDLE hash{};std::array<unsigned char,32> out{};
        require(BCryptCreateHash(alg,&hash,nullptr,0,key.data(),static_cast<ULONG>(key.size()),0)>=0,"HMAC creation failed");
        const auto a=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),64,0);
        const auto b=BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0);BCryptDestroyHash(hash);
        require(a>=0 && b>=0,"HMAC failed");return out;
    }
    Bytes encode(const Packet& p){
        Bytes b{};put(b,0,0x5343364e);put(b,1,protocol);put(b,2,p.kind);put(b,3,p.role);
        put(b,4,static_cast<unsigned>(p.nonce>>32));put(b,5,static_cast<unsigned>(p.nonce));
        put(b,6,static_cast<unsigned>(p.echo>>32));put(b,7,static_cast<unsigned>(p.echo));
        put(b,8,schema);put(b,9,simulation);put(b,10,frames);put(b,11,p.frame);put(b,12,p.value);put(b,13,p.sequence);
        auto tag=mac(b);std::copy(tag.begin(),tag.end(),b.begin()+64);return b;
    }
    bool authentic(const Bytes& b){
        auto tag=mac(b);unsigned difference=0;for(unsigned i=0;i<32;++i)difference|=tag[i]^b[64+i];
        return difference==0;
    }
    bool decode(const Bytes& b,Packet& p){
        if(!authentic(b) || get(b,0)!=0x5343364e || get(b,1)!=protocol || get(b,8)!=schema || get(b,9)!=simulation || get(b,10)!=frames || get(b,14) || get(b,15))return false;
        p={get(b,2),get(b,3),get(b,11),get(b,12),get(b,13),(std::uint64_t(get(b,4))<<32)|get(b,5),(std::uint64_t(get(b,6))<<32)|get(b,7)};
        return p.kind>=hello && p.kind<=result && p.role<2 && p.nonce!=0;
    }
};
struct State {unsigned hash{2166136261u},tick{};bool operator==(const State&)const=default;};
struct Adapter {
    State state;unsigned role;
    bool save(State& s){s=state;return true;}bool load(const State& s){state=s;return true;}
    bool step(unsigned local,unsigned remote){const auto a=role?remote:local,b=role?local:remote;
        state.hash=(state.hash^a)*16777619u;state.hash=(state.hash^b)*16777619u;++state.tick;return true;}
};
unsigned synthetic(unsigned role,unsigned frame){return ((frame/(role?5u:3u))+role)%4;}
struct Options {unsigned role{},port{},peer_port{},delay{},drop{},timeout{20};std::string ip,key;bool selftest{},relay{};};
unsigned number(const std::string& s){std::size_t n{};auto v=std::stoul(s,&n);require(n==s.size() && v<=65535,"Invalid numeric option");return static_cast<unsigned>(v);}
Options options(int argc,char** argv){
    Options o;bool role=false;
    for(int i=1;i<argc;++i){std::string k=argv[i];if(k=="--self-test"){o.selftest=true;continue;}
        if(k=="--relay-server"){o.relay=true;continue;}
        require(i+1<argc,"Missing option value");std::string v=argv[++i];
        if(k=="--role"){o.role=number(v);role=true;}else if(k=="--port")o.port=number(v);
        else if(k=="--peer-port")o.peer_port=number(v);else if(k=="--peer")o.ip=v;else if(k=="--key-file")o.key=v;
        else if(k=="--delay-ms")o.delay=number(v);else if(k=="--drop-every")o.drop=number(v);else if(k=="--timeout-seconds")o.timeout=number(v);
        else throw std::runtime_error("Unknown option");
    }
    require(!o.key.empty(),"--key-file is required");
    require(o.selftest || (o.relay && o.port) || (role && o.role<2 && o.port && o.peer_port && !o.ip.empty()),"Need --role 0|1 --port N --peer IPv4 --peer-port N --key-file PATH (or --relay-server --port N --key-file PATH)");
    require(o.delay<=500 && o.timeout>=3 && o.timeout<=(o.relay?600u:60u),"Delay limit 500 ms; timeout 3..60 seconds (relay up to 600)");return o;
}
struct Socket {
    SOCKET s{INVALID_SOCKET};
    Socket(unsigned port){WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"WSAStartup failed");
        s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);require(s!=INVALID_SOCKET,"socket failed");
        BOOL exclusive=TRUE;require(setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==0,"exclusive bind option failed");
        sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(static_cast<u_short>(port));addr.sin_addr.s_addr=htonl(INADDR_ANY);
        require(bind(s,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0,"UDP bind failed");
        u_long mode=1;require(ioctlsocket(s,FIONBIO,&mode)==0,"nonblocking socket failed");}
    ~Socket(){if(s!=INVALID_SOCKET)closesocket(s);WSACleanup();}
};
int run_relay(const Options& o,Crypto& crypto){
    Socket socket(o.port);
    struct Peer {sockaddr_in address{};std::uint64_t nonce{};};std::array<Peer,2> peers{};
    const auto start=Clock::now();auto refill=start,next_report=start;double tokens=512;
    unsigned forwarded=0,rejected=0;
    unsigned received=0,loopback=0,nonloopback=0,bad_size=0,bad_auth=0,bad_format=0,bad_registration=0,stale_session=0,rate_limited=0,socket_errors=0;
    std::array<unsigned,2> valid_role{};
    auto report=[&]{std::cout<<"RELAY STATUS received="<<received<<" loopback="<<loopback<<" nonloopback="<<nonloopback
        <<" auth_failed="<<bad_auth<<" malformed_size="<<bad_size<<" incompatible_or_invalid="<<bad_format
        <<" registration_rejected="<<bad_registration<<" stale_session="<<stale_session<<" rate_limited="<<rate_limited
        <<" socket_errors="<<socket_errors<<" valid_role0="<<valid_role[0]<<" valid_role1="<<valid_role[1]
        <<" forwarded="<<forwarded<<" rejected="<<rejected<<'\n'<<std::flush;};
    std::cout<<"RELAY READY diagnostics=2 udp_port="<<o.port<<" lifetime_seconds="<<o.timeout<<"; one authenticated pair; restart relay for a new run.\n"<<std::flush;
    while(Clock::now()-start<std::chrono::seconds(o.timeout)){
        const auto now=Clock::now();tokens=std::min(512.0,tokens+std::chrono::duration<double>(now-refill).count()*2000.0);refill=now;
        for(unsigned budget=0;budget<256;++budget){
            Bytes bytes{};sockaddr_in from{};int length=sizeof(from);
            int n=recvfrom(socket.s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&from),&length);
            if(n==SOCKET_ERROR){auto error=WSAGetLastError();if(error==WSAEWOULDBLOCK)break;
                if(error==WSAEMSGSIZE){++bad_size;++rejected;continue;}
                if(error==WSAECONNRESET){++socket_errors;++rejected;continue;}throw std::runtime_error("Relay receive failed");}
            ++received;
            const bool local=(ntohl(from.sin_addr.s_addr)>>24)==127;
            auto& source_count=local?loopback:nonloopback;
            if(++source_count==1){char address[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&from.sin_addr,address,sizeof(address));
                std::cout<<"First "<<(local?"loopback":"nonloopback")<<" datagram from="<<address<<":"<<ntohs(from.sin_port)<<" bytes="<<n<<'\n'<<std::flush;}
            if(tokens<1){++rate_limited;++rejected;continue;}tokens-=1;
            Packet p;
            if(n!=static_cast<int>(bytes.size())){++bad_size;++rejected;continue;}
            if(!crypto.authentic(bytes)){++bad_auth;++rejected;continue;}
            if(!crypto.decode(bytes,p)){++bad_format;++rejected;continue;}
            ++valid_role[p.role];
            auto& sender=peers[p.role];auto& other=peers[1-p.role];
            if(!sender.nonce){
                if(p.kind!=hello || p.echo || (other.nonce && other.address.sin_addr.s_addr==from.sin_addr.s_addr && other.address.sin_port==from.sin_port)){++bad_registration;++rejected;continue;}
                sender={from,p.nonce};std::cout<<"Registered role="<<p.role<<'\n'<<std::flush;
            }
            // Never let a third endpoint or restarted process take an occupied slot.
            if(sender.nonce!=p.nonce || sender.address.sin_addr.s_addr!=from.sin_addr.s_addr || sender.address.sin_port!=from.sin_port){++stale_session;++rejected;continue;}
            if(!other.nonce)continue;
            if(p.kind!=hello && p.echo!=other.nonce){++stale_session;++rejected;continue;}
            const int sent=sendto(socket.s,reinterpret_cast<const char*>(bytes.data()),n,0,reinterpret_cast<const sockaddr*>(&other.address),sizeof(other.address));
            if(sent==n)++forwarded;
            else if(WSAGetLastError()!=WSAEWOULDBLOCK)throw std::runtime_error("Relay send failed");
        }
        if(now>=next_report){report();next_report=now+std::chrono::seconds(2);}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    report();
    std::cout<<"Relay stopped: forwarded="<<forwarded<<" rejected="<<rejected<<'\n';return 0;
}
int run(const Options& o,Crypto& crypto){
    if(o.relay)return run_relay(o,crypto);
    if(o.selftest){
        Packet p{input,0,119,3,7,123,456},decoded;auto bytes=crypto.encode(p);
        require(crypto.decode(bytes,decoded) && decoded.nonce==123 && decoded.echo==456 && decoded.frame==119,"Codec round trip failed");
        for(unsigned n=0;n<bytes.size();++n){auto changed=bytes;changed[n]^=1;require(!crypto.decode(changed,decoded),"Tampered packet accepted");}
        bytes=crypto.encode(p);put(bytes,9,999);auto tag=crypto.mac(bytes);std::copy(tag.begin(),tag.end(),bytes.begin()+64);
        require(!crypto.decode(bytes,decoded),"Incompatible simulation accepted");
        std::cout<<"PASS: wire encoding, all 96 single-byte corruptions, authenticated compatibility mismatch\n";return 0;
    }
    Socket socket(o.port);sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_port=htons(static_cast<u_short>(o.peer_port));
    require(inet_pton(AF_INET,o.ip.c_str(),&peer.sin_addr)==1,"Peer must be a numeric IPv4 address");
    require(peer.sin_addr.s_addr!=INADDR_ANY && peer.sin_addr.s_addr!=INADDR_BROADCAST,"Invalid peer address");
    std::uint64_t nonce{};require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0 && nonce,"Nonce generation failed");
    std::uint64_t remote_nonce{};bool connected=false,done=false,peer_done=false;
    using Session=sc6::research::OfflineRollbackSession<unsigned,State>;
    Session session;Adapter live{{},o.role},oracle{{},o.role};std::array<bool,frames> received{};
    for(unsigned f=0;f<frames;++f)oracle.step(synthetic(o.role,f),synthetic(1-o.role,f));
    struct Pending{Clock::time_point due;Bytes bytes;};std::deque<Pending> pending;
    auto start=Clock::now(),next_control=start,next_frame=start,next_retry=start,next_ping=start,next_result=start,next_report=start,last_valid=start;
    unsigned udp_sent=0,udp_received=0;
    std::optional<Clock::time_point> finished_at;unsigned sent=0,dropped=0,rejected=0,sequence=0,ping_id=0;
    std::optional<unsigned> rtt;
    std::optional<Clock::time_point> ping_started;
    auto send=[&](unsigned kind,unsigned frame=0,unsigned value=0){
        ++sent;if(o.drop && sent%o.drop==0){++dropped;return;}
        require(pending.size()<4096,"Outbound queue limit exceeded");
        pending.push_back({Clock::now()+std::chrono::milliseconds(o.delay),crypto.encode({kind,o.role,frame,value,++sequence,nonce,remote_nonce})});
    };
    std::cout<<"NETWORK DIAGNOSTIC: synthetic simulation only; no SC6 hooks. Waiting for authenticated peer.\n"<<std::flush;
    std::cout<<"CLIENT CONFIG diagnostics=2 role="<<o.role<<" local_port="<<o.port<<" destination="<<o.ip<<":"<<o.peer_port<<'\n'<<std::flush;
    while(Clock::now()-start<std::chrono::seconds(o.timeout)){
        const auto now=Clock::now();
        if(now>=next_control){send(remote_nonce?ready:hello);next_control=now+std::chrono::milliseconds(100);}
        while(!pending.empty() && pending.front().due<=now){auto& bytes=pending.front().bytes;
            int n=sendto(socket.s,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
            if(n==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK)break;
            require(n==static_cast<int>(bytes.size()),"UDP send failed");++udp_sent;pending.pop_front();}
        for(unsigned budget=0;budget<512;++budget){Bytes bytes{};sockaddr_in from{};int length=sizeof(from);
            int n=recvfrom(socket.s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&from),&length);
            if(n==SOCKET_ERROR){auto e=WSAGetLastError();if(e==WSAEWOULDBLOCK)break;if(e==WSAEMSGSIZE || e==WSAECONNRESET){++rejected;continue;}throw std::runtime_error("UDP receive failed");}
            Packet p;
            ++udp_received;
            if(n!=static_cast<int>(bytes.size()) || from.sin_addr.s_addr!=peer.sin_addr.s_addr || from.sin_port!=peer.sin_port || !crypto.decode(bytes,p) || p.role==o.role){++rejected;continue;}
            if(p.kind==hello || p.kind==ready){
                if(remote_nonce && p.nonce!=remote_nonce){++rejected;continue;}
                if(p.kind==ready && p.echo!=nonce){++rejected;continue;}
                remote_nonce=p.nonce;
                if(p.kind==ready && !connected){connected=true;next_frame=now;last_valid=now;std::cout<<"Handshake confirmed; protocol/schema/synthetic build match.\n"<<std::flush;}
                if(connected)last_valid=now;
                continue;
            }
            if(!connected || p.nonce!=remote_nonce || p.echo!=nonce){++rejected;continue;}
            last_valid=now;
            if(p.kind==input){require(p.frame<frames && p.value<4,"Invalid authenticated input");
                auto arrival=session.receive(p.frame,p.value);require(arrival!=Session::Arrival::conflict && arrival!=Session::Arrival::out_of_range,"Input conflict or frame outside history");received[p.frame]=true;
            }else if(p.kind==ping)send(pong,p.frame);
            else if(p.kind==pong && ping_started && p.frame==ping_id){rtt=static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(now-*ping_started).count());ping_started.reset();}
            else if(p.kind==result){require(p.frame==frames && p.value==oracle.state.hash,"Peer final state mismatch");peer_done=true;}
        }
        if(now>=next_report){std::cout<<"CLIENT STATUS udp_sent="<<udp_sent<<" udp_received="<<udp_received<<" rejected="<<rejected<<" handshake="<<(connected?"confirmed":"pending")<<'\n'<<std::flush;next_report=now+std::chrono::seconds(5);}
        if(connected){
            require(now-last_valid<std::chrono::seconds(5),"Peer disconnected; session stopped");
            require(session.reconcile(live),"Rollback reconciliation failed");
            if(session.head()<frames && now>=next_frame){auto f=static_cast<unsigned>(session.head());auto value=synthetic(o.role,f);
                require(session.advance(live,value),"Simulation advance failed");send(input,f,value);next_frame=now+std::chrono::milliseconds(16);}
            if(now>=next_retry){for(unsigned f=0;f<session.head();++f)send(input,f,synthetic(o.role,f));next_retry=now+std::chrono::milliseconds(150);}
            if(now>=next_ping){ping_id++;ping_started=now;send(ping,ping_id);next_ping=now+std::chrono::seconds(2);}
            if(!done && session.head()==frames && std::all_of(received.begin(),received.end(),[](bool v){return v;})){
                require(live.state==oracle.state,"Corrected state differs from zero-delay oracle");done=true;}
            if(done && now>=next_result){send(result,frames,live.state.hash);next_result=now+std::chrono::milliseconds(100);}
            // Results are retransmitted while both peers finish and drain late datagrams.
            if(done && peer_done){if(!finished_at)finished_at=now;
                if(now-*finished_at>std::chrono::seconds(2)){
                    std::cout<<"PASS frames="<<frames<<" corrections="<<session.corrections()<<" replayed="<<session.replayed()<<" rtt_ms="<<(rtt?std::to_string(*rtt):"unavailable")<<" sent="<<sent<<" simulated_drops="<<dropped<<" rejected="<<rejected<<" final_hash="<<live.state.hash<<" peer_match=1\n";return 0;}}
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Timeout: handshake or final two-peer comparison incomplete");
}
}
int main(int argc,char** argv){try{auto o=options(argc,argv);Crypto crypto(o.key);return run(o,crypto);}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
