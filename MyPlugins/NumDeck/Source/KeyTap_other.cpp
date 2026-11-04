#include "KeyTap.h"

// Mac 以外ではテンキーの横取りはしない（ビルドが通るだけの空の実装）
namespace numdeck
{
    struct KeyTap::Impl {};
    KeyTap::KeyTap() : impl (std::make_unique<Impl>()) {}
    KeyTap::~KeyTap() = default;
    bool KeyTap::start()                   { return false; }
    bool KeyTap::isRunning() const         { return false; }
    bool KeyTap::hasPermission()           { return false; }
    void KeyTap::requestPermission()       {}
    void KeyTap::resetPermission()         {}
    void KeyTap::openPermissionSettings()  {}
    bool KeyTap::isLogicFrontmost()        { return false; }
    bool KeyTap::isLogicRunning()          { return false; }
    void KeyTap::rememberFrontApp()        {}
    void KeyTap::activateLastApp()         {}
    void KeyTap::sendKey (int, int)        {}
    void KeyTap::sendKeyToLogic (int, int) {}
    void KeyTap::sendRemote (const std::string&, const std::string&) {}
    bool KeyTap::isRefMonitorFrontmost()   { return false; }
    void KeyTap::openApp (const std::string&) {}
    void KeyTap::openURL (const std::string&, const std::string&) {}
    void KeyTap::openURLSplit (const std::string&, const std::string&, const std::string&) {}
    void KeyTap::openURLPlaced (const std::string&, const std::string&, const std::string&, const std::string&) {}
    std::vector<std::string> KeyTap::screenNames() { return {}; }
    bool KeyTap::bringLogicFront() { return false; }
    void KeyTap::returnFromLogic() {}
    void KeyTap::quitAllApps() {}
    void KeyTap::toggleFullScreenOrSplit() {}
    std::vector<std::pair<std::string, std::string>> KeyTap::chromeProfiles() { return {}; }
    bool KeyTap::chromeFrontPage (std::string&, std::string&) { return false; }
    std::string KeyTap::appDisplayName (const std::string& path) { return path; }
    std::vector<unsigned char> KeyTap::appIconPNG (const std::string&, int) { return {}; }
}
