#pragma once

#include <filesystem>

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/NativeMod.h"

namespace aeris {

class AerisMod {
public:
    static AerisMod& getInstance();

    AerisMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

    [[nodiscard]] std::filesystem::path getPacksDir() const;
    [[nodiscard]] std::filesystem::path getGeneratedDir() const;

private:
    ll::mod::NativeMod&    mSelf;
    ll::event::ListenerPtr mCommandListener;
    ll::event::ListenerPtr mBeforeUiRenderListener;
    ll::event::ListenerPtr mAfterUiRenderListener;
    ll::event::ListenerPtr mMouseInputListener;
    ll::event::ListenerPtr mKeyInputListener;
};

} // namespace aeris
