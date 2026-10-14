#pragma once
#include "NativeAdjacentRestore.hpp"
namespace sc6::research {
// General combat needs structural checks for every pair first, then live
// preparation immediately before each reverse load. Historical intermediate
// states cannot be compared to the live endpoint before newer loads complete.
struct OwnedWindowRestorePreparation {
    template<class Workspace> static bool preflight(Workspace& work,
        NativeBoundarySnapshot& source,NativeBoundarySnapshot& next){
        using M=NativeBoundarySnapshot::Mode;
        if(source.mode==M::general_combat || next.mode==M::general_combat)
            return work.preflight_general(source,next);
        return work.prepare_captured(source,next,next.mode==M::direction_observation,false,false);
    }
    template<class Workspace> static bool before_load(Workspace& work,
        NativeBoundarySnapshot& source,NativeBoundarySnapshot& next){
        using M=NativeBoundarySnapshot::Mode;
        if(source.mode==M::general_combat || next.mode==M::general_combat)
            return work.prepare_captured(source,next,false,false,true);
        return true;
    }
};
}
