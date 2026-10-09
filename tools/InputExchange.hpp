// Included by NetworkProbe.cpp to reuse its bounded wire codec and socket setup.
#include "../src/InputObservation.hpp"
namespace {
struct ObservationView {
    HANDLE mapping{};const sc6::observation::Shared* shared{};
    explicit ObservationView(unsigned pid){
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,sc6::observation::mapping_name(pid).c_str());
        require(mapping!=nullptr,"Observer DLL mapping missing: install build and launch SC6 first");
        shared=static_cast<const sc6::observation::Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(sc6::observation::Shared)));
        if(!shared){CloseHandle(mapping);mapping=nullptr;throw std::runtime_error("Cannot map observer output");}
    }
    ~ObservationView(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);}
};
int run_observation(const Options& o,Crypto& crypto){
    using namespace sc6::observation;
    require(o.delay==0,"Observation delay injection is not supported; use the synthetic diagnostic for delay tests");
    ObservationView view(o.observe_pid);const auto* shared=view.shared;
    require(shared->signature==magic && shared->schema==version && shared->pid==o.observe_pid && shared->status==0,"Observer build mismatch or already consumed; restart SC6");
    const auto directory=std::filesystem::path(o.capture_dir);
    require(std::filesystem::is_directory(directory),"Capture directory missing");
    for(const auto& f:std::filesystem::directory_iterator(directory))require(f.path().extension()!=L".arm","Another capture is already armed");
    Socket socket(o.port);sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_port=htons(static_cast<u_short>(o.peer_port));
    require(inet_pton(AF_INET,o.ip.c_str(),&peer.sin_addr)==1,"Peer must be numeric IPv4");
    std::uint64_t nonce{};require(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0 && nonce,"Nonce generation failed");
    std::uint64_t remote_nonce{};bool connected=false,armed=false,peer_done=false;
    constexpr unsigned chunks=limit*parts;
    std::array<Record,limit> local{},remote{};std::array<bool,chunks> acknowledged{},received{};
    unsigned published=0,remote_chunks=0,sent=0,rejected=0;
    auto start=Clock::now(),control=start,retry=start,result_time=start;
    std::optional<Clock::time_point> completed;
    auto send=[&](unsigned kind,unsigned frame=0,unsigned value=0,unsigned word=0){
        ++sent;if(o.drop && sent%o.drop==0)return;
        const auto bytes=crypto.encode({kind,o.role,frame,value,word,nonce,remote_nonce});
        const int count=sendto(socket.s,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&peer),sizeof(peer));
        if(count==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK)return;
        require(count==static_cast<int>(bytes.size()),"Observation send failed");
    };
    auto export_records=[&](const char* label,const std::array<Record,limit>& records){
        const auto path=directory/("network-"+std::to_string(nonce)+"-"+label+".csv");
        std::ofstream out(path);out<<"sample,outer,qpc,p0_word0,p0_word1,p1_word0,p1_word1,aux0,aux1,aux2,aux3,aux4,aux5\n";
        for(unsigned f=0;f<limit;++f){const auto& w=records[f].data;out<<f<<','<<w[0]<<','<<((std::uint64_t(w[12])<<32)|w[11]);
            for(unsigned i=1;i<=10;++i)out<<','<<w[i];out<<'\n';}
        out.flush();require(bool(out),"Could not save observation CSV");
        std::cout<<"Observation CSV: "<<path.string()<<'\n';
    };
    std::cout<<"OBSERVATION ONLY: real input envelopes; no injection, restore, game synchronization, or automatic match-mode detection.\n"
        <<"CLIENT CONFIG role="<<o.role<<" destination="<<o.ip<<":"<<o.peer_port<<" pid="<<o.observe_pid<<'\n'<<std::flush;
    while(Clock::now()-start<std::chrono::seconds(o.timeout)){
        const auto now=Clock::now();
        if(now>=control){send(remote_nonce?ready:hello);control=now+std::chrono::milliseconds(100);}
        for(unsigned budget=0;budget<512;++budget){Bytes bytes{};sockaddr_in from{};int size=sizeof(from);
            const int n=recvfrom(socket.s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&from),&size);
            if(n==SOCKET_ERROR){auto e=WSAGetLastError();if(e==WSAEWOULDBLOCK)break;if(e==WSAECONNRESET || e==WSAEMSGSIZE){++rejected;continue;}throw std::runtime_error("Observation receive failed");}
            Packet p;if(n!=static_cast<int>(bytes.size()) || from.sin_addr.s_addr!=peer.sin_addr.s_addr || from.sin_port!=peer.sin_port || !crypto.decode(bytes,p) || p.role==o.role){++rejected;continue;}
            if(p.kind==hello || p.kind==ready){
                if((remote_nonce && p.nonce!=remote_nonce) || (p.kind==ready && p.echo!=nonce)){++rejected;continue;}
                remote_nonce=p.nonce;if(p.kind==ready)connected=true;continue;
            }
            if(!connected || p.nonce!=remote_nonce || p.echo!=nonce){++rejected;continue;}
            if(p.kind==observation){
                require(p.frame<chunks,"Observation index out of range");auto& w=remote[p.frame/parts].data;const auto offset=(p.frame%parts)*2;
                if(received[p.frame])require(w[offset]==p.value && w[offset+1]==p.sequence,"Conflicting remote observation");
                else{w[offset]=p.value;w[offset+1]=p.sequence;received[p.frame]=true;++remote_chunks;}
                send(receipt,p.frame);
            }else if(p.kind==receipt){require(p.frame<chunks && p.frame/parts<published,"Receipt for unsent observation");acknowledged[p.frame]=true;}
            else if(p.kind==observation_result){
                if(published==limit && remote_chunks==chunks){require(p.frame==limit && p.value==checksum(remote) && p.sequence==checksum(local),"Peer observation checksums disagree");peer_done=true;}
            }else {++rejected;}
        }
        if(connected && !armed){
            // File control only. The game mapping remains read-only to this process.
            const auto arm=directory/"network-observe.arm";
            HANDLE file=CreateFileW(arm.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            require(file!=INVALID_HANDLE_VALUE,"Cannot create observation arm (another capture may exist)");
            const auto text="NETWORK_OBSERVE_ONLY "+std::to_string(o.observe_pid)+" "+std::to_string(nonce)+"\n";DWORD written{};
            const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE;CloseHandle(file);
            require(ok && written==text.size(),"Observation arm write failed");armed=true;
            std::cout<<"Handshake confirmed; observing the next 120 frame boundaries.\n"<<std::flush;
        }
        if(armed && shared->nonce==nonce){
            const LONG count=shared->count;MemoryBarrier();require(count>=0 && count<=static_cast<LONG>(limit),"Observer count invalid");
            require(shared->status!=3,"Game observation failed or timed out");
            while(published<static_cast<unsigned>(count)){local[published]=shared->records[published];++published;}
            if(now>=retry){
                for(unsigned chunk=0;chunk<published*parts;++chunk)if(!acknowledged[chunk]){const auto& w=local[chunk/parts].data;const auto offset=(chunk%parts)*2;send(observation,chunk,w[offset],w[offset+1]);}
                retry=now+std::chrono::milliseconds(150);
            }
        }
        const bool local_done=published==limit && shared->status==2 && remote_chunks==chunks && std::all_of(acknowledged.begin(),acknowledged.end(),[](bool b){return b;});
        if(local_done && now>=result_time){send(observation_result,limit,checksum(local),checksum(remote));result_time=now+std::chrono::milliseconds(100);}
        if(local_done && peer_done){if(!completed)completed=now;
            if(now-*completed>std::chrono::seconds(2)){
                export_records("local",local);export_records("remote",remote);
                unsigned local_gaps=0,remote_gaps=0;for(unsigned f=1;f<limit;++f){local_gaps+=local[f].data[0]!=local[f-1].data[0]+1;remote_gaps+=remote[f].data[0]!=remote[f-1].data[0]+1;}
                std::cout<<"PASS observation_transport_only=1 records="<<limit<<" local_checksum="<<checksum(local)<<" remote_checksum="<<checksum(remote)
                    <<" local_outer_gaps="<<local_gaps<<" remote_outer_gaps="<<remote_gaps<<" sent="<<sent<<" rejected="<<rejected
                    <<" peer_match=1 game_alignment_verified=0 restore=0 injection=0\n";return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Observation timeout: peer, game frame capture, or delivery incomplete; restart SC6 before rearming");
}
}
