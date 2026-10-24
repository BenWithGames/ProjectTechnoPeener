#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "FrameRecorder.hpp"
#include "StatusOverlay.hpp"
#include "SteamDiagnostic.hpp"
#include "SteamObserver.hpp"
#include "SteamHandshake.hpp"
#include "AutomaticSteam.hpp"
#include "ConnectionSound.hpp"

class SC6RollbackMod final : public RC::CppUserModBase {
    sc6::StatusOverlay overlay;
    sc6::ConnectionSound connection_sound;
public:
    SC6RollbackMod() {
        ModName = STR("SC6Rollback");
        ModVersion = STR("0.2.0");
        ModDescription = STR("Offline rollback research scaffold");
        ModAuthors = STR("SC6Rollback contributors");
        RC::Output::send<RC::LogLevel::Verbose>(STR("[SC6Rollback] loaded; offline recorder; requires explicit arm\n"));
    }

    auto on_unreal_init() -> void override {
        try {
            wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
            const auto mod=std::filesystem::path(exe).parent_path()/L"Mods/SC6Rollback";
            sc6::automatic_play::enabled=std::filesystem::exists(mod/L"automatic-play.enabled");
            if(sc6::automatic_play::enabled)std::filesystem::create_directories(mod/L"logs");
            sc6::recorder_init(); overlay.start();
            connection_sound.start((mod/L"connection.mp3").wstring());
        } catch (...) { RC::Output::send<RC::LogLevel::Error>(STR("[SC6Rollback] recorder initialization failed\n")); }
    }

    auto on_update() -> void override {
        try { sc6::steam_diagnostic_poll(); } catch (...) {}
        try { sc6::steam_observer::poll(); } catch (...) { sc6::steam_observer::active=false; }
        try { sc6::steam_handshake::poll(); } catch (...) { sc6::steam_handshake::stopped=true; }
        try { sc6::automatic_steam::poll(); } catch (...) { sc6::automatic_steam::disabled=true;sc6::automatic_steam::last_received=0;sc6::automatic_steam::status="error_default_networking"; }
        const bool ping_peer=sc6::automatic_steam::compatible(GetTickCount64());
        connection_sound.update(ping_peer?sc6::automatic_steam::peer:0);
        sc6::automatic_play::requested_peer=ping_peer?sc6::automatic_steam::peer:0;
        sc6::overlay_ping.configure(ping_peer?sc6::steam_observer::observed:nullptr,
            ping_peer?sc6::automatic_steam::peer:0,sc6::automatic_steam::protocol.local_nonce,sc6::automatic_steam::protocol.remote_nonce);
        auto stats=sc6::steam_handshake::running?sc6::steam_handshake::overlay_stats():sc6::automatic_steam::overlay_stats();
        stats.ping_ms=stats.connected?sc6::overlay_ping.value():std::nullopt;
        overlay.publish(stats);
        try { sc6::recorder_poll(); } catch (...) { sc6::recorder_stop(); }
    }
    ~SC6RollbackMod() override { sc6::overlay_ping.stop(); sc6::research::fresh_raw_sender.stop(); sc6::steam_handshake::stopped=true; sc6::steam_observer::active=false; overlay.stop(); sc6::recorder_stop(); }
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
