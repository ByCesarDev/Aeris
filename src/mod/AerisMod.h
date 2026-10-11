#pragma once

#include <filesystem>
#include <string_view>

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/NativeMod.h"

class CommandOrigin;
class Player;

namespace aeris {

class AerisMod {
public:
    static AerisMod& getInstance();

    AerisMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

    void registerCommands(bool isClient);
    void executeAerisCommand(std::string_view args, CommandOrigin* origin = nullptr, Player* player = nullptr);

    [[nodiscard]] std::filesystem::path getPacksDir() const;
    [[nodiscard]] std::filesystem::path getGeneratedDir() const;

private:
    ll::mod::NativeMod&    mSelf;
    ll::event::ListenerPtr mServerCommandListener;
    ll::event::ListenerPtr mClientCommandListener;
    ll::event::ListenerPtr mExecutingCommandListener;
    ll::event::ListenerPtr mPlayerChatListener;
    ll::event::ListenerPtr mServerStartedListener;
    ll::event::ListenerPtr mClientJoinLevelListener;
};

} // namespace aeris
