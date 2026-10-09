#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "FrameRecorder.hpp"

class SC6RollbackMod final : public RC::CppUserModBase {
public:
    SC6RollbackMod() {
        ModName = STR("SC6Rollback");
        ModVersion = STR("0.2.0");
        ModDescription = STR("Offline rollback research scaffold");
        ModAuthors = STR("SC6Rollback contributors");
        RC::Output::send<RC::LogLevel::Verbose>(STR("[SC6Rollback] loaded; offline recorder; requires explicit arm\n"));
    }

    auto on_unreal_init() -> void override {
        try { sc6::recorder_init(); } catch (...) { RC::Output::send<RC::LogLevel::Error>(STR("[SC6Rollback] recorder initialization failed\n")); }
    }

    auto on_update() -> void override {
        try { sc6::recorder_poll(); } catch (...) { sc6::recorder_stop(); }
    }
    ~SC6RollbackMod() override { sc6::recorder_stop(); }
    // on_update only handles control and output; samples come from EntryBridge.
};

extern "C" {
__declspec(dllexport) RC::CppUserModBase* start_mod() {
    return new SC6RollbackMod();
}
__declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) {
    delete mod;
}
}
