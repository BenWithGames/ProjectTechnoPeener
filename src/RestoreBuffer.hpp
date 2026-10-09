#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>

namespace sc6::research {
// Local-only prototype. Deliberately has no game entry point or arm-file support.
// Throwing prevents an ignored short-read return from becoming silent success.
// This exception mechanism is NOT approved for unwinding through the game.
class RestoreBuffer {
public:
    enum class Error { none, overread, null_destination, copy_failed, incomplete };
    struct Failure : std::runtime_error {
        explicit Failure(Error e):std::runtime_error("Restore buffer rejected transfer"),reason(e){}
        Error reason;
    };
    using Copier=bool (*)(void*,const void*,std::size_t);
    explicit RestoreBuffer(std::span<const std::byte> bytes,Copier copier=local_copy)
        :bytes_(bytes),copier_(copier) {}
    void read(void* destination,std::uint64_t length) {
        if(error_!=Error::none) throw Failure(error_);
        if(length>bytes_.size()-position_) fail(Error::overread);
        if(!length) return;
        if(!destination) fail(Error::null_destination);
        if(!copier_ || !copier_(destination,bytes_.data()+position_,static_cast<std::size_t>(length))) fail(Error::copy_failed);
        position_+=static_cast<std::size_t>(length); ++transfers_;
    }
    void finish() {
        if(error_!=Error::none) throw Failure(error_);
        if(position_!=bytes_.size()) fail(Error::incomplete);
    }
    std::size_t position() const {return position_;}
    std::size_t transfers() const {return transfers_;}
    Error error() const {return error_;}
private:
    static bool local_copy(void* d,const void* s,std::size_t n){std::memcpy(d,s,n);return true;}
    [[noreturn]] void fail(Error e){error_=e;throw Failure(e);}
    std::span<const std::byte> bytes_;
    Copier copier_;
    std::size_t position_{},transfers_{};
    Error error_=Error::none;
};
}
