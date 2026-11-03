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
    void KeyTap::openPermissionSettings()  {}
    bool KeyTap::isLogicFrontmost()        { return false; }
    bool KeyTap::isLogicRunning()          { return false; }
    void KeyTap::sendKey (int, int)        {}
    void KeyTap::openApp (const std::string&) {}
    void KeyTap::openURL (const std::string&) {}
    bool KeyTap::chromeFrontPage (std::string&, std::string&) { return false; }
    std::string KeyTap::appDisplayName (const std::string& path) { return path; }
    std::vector<unsigned char> KeyTap::appIconPNG (const std::string&, int) { return {}; }
}
