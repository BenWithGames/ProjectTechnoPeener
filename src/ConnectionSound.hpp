#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstdint>
#pragma comment(lib, "winmm.lib")
namespace sc6 {
class ConnectionSound {
    std::mutex mutex_; std::condition_variable wake_; std::thread worker_;
    bool stop_{}, pending_{}; std::uint64_t peer_{};
public:
    void start(std::wstring path) {
        worker_=std::thread([this,path=std::move(path)] {
            // All decoder/file work stays off the game update thread.
            bool opened=false;
            std::unique_lock lock(mutex_);
            for (;;) {
                wake_.wait(lock,[this]{return stop_||pending_;});
                if(stop_)break;
                pending_=false;lock.unlock();
                if(!opened) {
                    const auto command=L"open \""+path+L"\" type mpegvideo alias sc6_connection_sound";
                    opened=mciSendStringW(command.c_str(),nullptr,0,nullptr)==0;
                }
                if(opened) {
                    mciSendStringW(L"setaudio sc6_connection_sound volume to 750",nullptr,0,nullptr);
                    mciSendStringW(L"seek sc6_connection_sound to start",nullptr,0,nullptr);
                    mciSendStringW(L"play sc6_connection_sound",nullptr,0,nullptr);
                }
                lock.lock();
            }
            lock.unlock();
            if(opened)mciSendStringW(L"close sc6_connection_sound",nullptr,0,nullptr);
        });
    }
    void update(std::uint64_t peer) {
        if(peer==peer_)return;
        peer_=peer;
        if(peer) {std::lock_guard lock(mutex_);pending_=true;wake_.notify_one();}
    }
    ~ConnectionSound() {
        {std::lock_guard lock(mutex_);stop_=true;}wake_.notify_one();
        if(worker_.joinable())worker_.join();
    }
};
}
