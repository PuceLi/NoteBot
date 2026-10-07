#pragma once

#include "ll/api/mod/NativeMod.h"

namespace notebot {

void installInputHooks();
void uninstallInputHooks();

class NoteBot {
public:
    static NoteBot& getInstance();

    NoteBot() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace notebot
