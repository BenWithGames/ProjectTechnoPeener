#pragma once
#include <cstdint>
#include <sstream>
#include <iomanip>
namespace sc6::automatic_report {
inline std::string match_id(uint64_t local,uint64_t remote){
    if(!local || !remote)return {};
    if(local>remote){const auto swap=local;local=remote;remote=swap;}
    std::ostringstream id;id<<std::hex<<std::setfill('0')<<std::setw(16)<<local<<std::setw(16)<<remote;
    return id.str();
}
}
