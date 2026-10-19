#pragma once
#include "InputObservation.hpp"
#include "NetworkInputComposer.hpp"
#include "NetworkReplayEligibility.hpp"
#include <map>
#include <string_view>
namespace sc6 {
// Network-thread-owned staging only. Nothing here calls game code. Counter
// evidence is not a round identity and must never authorize native restoration.
struct LiveInputComposition {
    enum class Purpose {comparison,authoritative_peer};
    Purpose purpose{Purpose::comparison};
    unsigned local_slot{},peer_slot{},imported_local{},validated_remote{};
    unsigned matched{},transitions{},auxiliary_differences{};
    unsigned round_matches{},round_missing{},round_mismatches{};
    unsigned local_round_gaps{},remote_round_gaps{};
    uint32_t last_gap_before{},last_gap_after{},last_gap_round{},last_gap_boundary{};
    std::array<bool,observation::capacity> round_checked{};
    const char* failure="none";
    std::array<bool,observation::capacity> imported{},staged{};
    std::array<NetworkInputComposer::Envelope,observation::capacity> envelopes{};
    std::map<uint32_t,unsigned> remote_index;
    uint64_t replay_local_nonce{},replay_peer_nonce{};
    unsigned replay_eligible{},replay_round_rejected{},replay_other_rejected{};
    std::array<std::array<bool,3>,observation::capacity> replay_checked{};
    bool bind_replay_session(uint64_t local,uint64_t peer){
        if(!local || !peer || (replay_local_nonce && (replay_local_nonce!=local || replay_peer_nonce!=peer)))
            return fail("replay_session_changed");
        replay_local_nonce=local;replay_peer_nonce=peer;return true;
    }
    bool fail(const char* reason){failure=reason;return false;}
    bool configure(unsigned slot,Purpose selected=Purpose::comparison){
        if(slot<1 || slot>2 || local_slot)return fail("composition_invalid_local_slot");
        local_slot=slot;purpose=selected;return true;
    }
    bool announce(unsigned slot){
        if(slot<1 || slot>2 || !local_slot || slot==local_slot || (peer_slot && peer_slot!=slot))
            return fail("composition_peer_slot_conflict");
        peer_slot=slot;return true;
    }
    bool consecutive_or_round_gap(uint32_t before,uint32_t after,unsigned i,
            const std::array<observation::Round,observation::capacity>* rounds,bool remote){
        if(before!=UINT32_MAX && after==before+1u)return true;
        // Owned live recording pauses while the native outer updates continue
        // between rounds. This permits staging only, never cross-round replay.
        if(purpose!=Purpose::authoritative_peer || !rounds || !replay_local_nonce || !replay_peer_nonce || after<=before)return false;
        const auto a=(*rounds)[i-1],b=(*rounds)[i];
        if(!a.number || a.number==UINT32_MAX || b.number!=a.number+1u ||
           !a.event_counter || b.event_counter<=a.event_counter ||
           b.event_counter<=before || b.event_counter>after)return false;
        if(remote)++remote_round_gaps;else ++local_round_gaps;
        last_gap_before=before;last_gap_after=after;last_gap_round=b.number;last_gap_boundary=b.event_counter;
        return true;
    }
    bool update(const std::array<observation::Record,observation::capacity>& local,unsigned count,
                const std::array<observation::Record,observation::capacity>& remote,
                const std::array<bool,observation::capacity>& seen,
                const std::array<observation::Round,observation::capacity>* local_rounds=nullptr,
                const std::array<observation::Round,observation::capacity>* remote_rounds=nullptr){
        if(failure!=std::string_view("none"))return false;
        if(count>observation::capacity)return fail("composition_count_invalid");
        if(purpose==Purpose::authoritative_peer && (!local_rounds || !remote_rounds || !replay_local_nonce || !replay_peer_nonce))
            return fail("composition_authority_identity_missing");
        while(imported_local<count){
            unsigned i=imported_local++;
            if(i && !consecutive_or_round_gap(local[i-1].data[0],local[i].data[0],i,local_rounds,false))return fail("composition_local_counter_reset");
            if(i){bool changed=false;for(unsigned w=1;w<=4;++w)changed|=local[i].data[w]!=local[i-1].data[w];
                transitions+=changed;}
        }
        for(unsigned i=0;i<observation::capacity;++i)if(seen[i]&&!imported[i]){
            if(!remote_index.emplace(remote[i].data[0],i).second)return fail("composition_duplicate_counter");
            imported[i]=true;
        }
        while(validated_remote<observation::capacity && seen[validated_remote]){
            unsigned i=validated_remote++;
            if(i && !consecutive_or_round_gap(remote[i-1].data[0],remote[i].data[0],i,remote_rounds,true))return fail("composition_remote_counter_reset");
        }
        if(!peer_slot)return true;
        for(unsigned i=0;i<count;++i)if(!staged[i]){
            auto found=remote_index.find(local[i].data[0]);
            if(found==remote_index.end() || found->second>=validated_remote)continue;
            const auto& p=remote[found->second];
            if(local_rounds || remote_rounds){
                if(!local_rounds || !remote_rounds)return fail("composition_round_metadata_missing");
                const auto a=(*local_rounds)[i],b=(*remote_rounds)[found->second];
                if(!round_checked[i]){
                    round_checked[i]=true;
                    if(!a.number || !b.number)++round_missing;
                    else if(a.number!=b.number || a.event_counter!=b.event_counter){
                        ++round_mismatches;return fail("composition_round_boundary_mismatch");
                    }else ++round_matches;
                }
                // Unknown round metadata never authorizes staging.
                if(!a.number || !b.number)continue;
            }
            if(purpose==Purpose::comparison)
                for(unsigned w=1;w<=4;++w)if(local[i].data[w]!=p.data[w])return fail("composition_button_alignment_mismatch");
            NetworkInputComposer::Envelope a{},b{};
            for(unsigned w=0;w<10;++w){a[w]=local[i].data[w+1];b[w]=p.data[w+1];}
            auto composed=NetworkInputComposer::compose(a,b,peer_slot,local[i].data[0],p.data[0]);
            if(!composed || (purpose==Purpose::comparison && *composed!=a))return fail("composition_preservation_mismatch");
            bool differs=false;for(unsigned w=4;w<10;++w)differs|=a[w]!=b[w];
            auxiliary_differences+=differs;envelopes[i]=*composed;staged[i]=true;++matched;
        }
        if(local_rounds && remote_rounds && replay_local_nonce && replay_peer_nonce){
            auto key=[&](unsigned i){const auto r=(*local_rounds)[i];
                return NetworkReplayEligibility::Key{replay_local_nonce,replay_peer_nonce,local_slot,r.number,r.event_counter,local[i].data[0]};};
            for(unsigned i=0;i<count;++i)for(unsigned depth=1;depth<=3 && i+depth<count;++depth){
                if(replay_checked[i][depth-1])continue;
                bool ready=staged[i];for(unsigned j=1;j<=depth;++j)ready=ready && staged[i+j];
                if(!ready)continue;
                std::array<NetworkReplayEligibility::Key,3> path{};
                for(unsigned j=0;j<depth;++j)path[j]=key(i+j+1);
                auto result=NetworkReplayEligibility::check(key(i),std::span(path.data(),depth));
                replay_checked[i][depth-1]=true;
                if(result==NetworkReplayEligibility::Result::eligible)++replay_eligible;
                else if(result==NetworkReplayEligibility::Result::round)++replay_round_rejected;
                else ++replay_other_rejected;
            }
        }
        return true;
    }
};
}
