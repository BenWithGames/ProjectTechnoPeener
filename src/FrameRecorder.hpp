#pragma once
namespace sc6 { void recorder_init(); void recorder_poll(); void recorder_stop(); }

namespace sc6 { inline bool (*prepare_owned_replay)(){}; }
