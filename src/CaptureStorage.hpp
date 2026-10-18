#pragma once
#include <filesystem>
#include <fstream>
#include <string>
namespace sc6::research {
// Report-worker only. Keep the first reference and latest three verified cycles.
// Failed/unverified windows and every numeric summary are never removed here.
inline bool prune_verified_cycle(const std::filesystem::path& report,unsigned current){
    if(current<=4)return true;
    const auto stem=report.stem().string()+"-cycle-"+std::to_string(current-3);
    std::ifstream summary(report.parent_path()/(stem+"-summary.txt"));
    bool verified=false;std::string line;
    while(std::getline(summary,line))if(line=="verified=1" || line=="verified=1\r")verified=true;
    if(!verified)return true;
    for(const auto* suffix:{"-owned-snapshot-0.bin","-owned-snapshot-1.bin","-owned-snapshot-2.bin","-owned-snapshot-3.bin","-replay-evidence.bin","-animation-copy.bin","-inline-rng.bin"}){
        std::error_code ec;std::filesystem::remove(report.parent_path()/(stem+suffix),ec);
        if(ec)return false;
    }
    return true;
}
inline bool capture_storage_available(const std::filesystem::path& directory){
    std::error_code ec;const auto info=std::filesystem::space(directory,ec);
    return !ec && info.available>=256ULL*1024*1024;
}
}
