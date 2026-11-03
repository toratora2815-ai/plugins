#include "KeyTap.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace numdeck
{
    namespace
    {
        // 自分が送ったキーの目印（自分のタップで拾い直さないため）
        constexpr int64_t kMagic = 0x4E554D44; // "NUMD"

        int padFromKeyCode (int vk)
        {
            switch (vk)
            {
                case 82: return Pad0;  case 83: return Pad1;  case 84: return Pad2;
                case 85: return Pad3;  case 86: return Pad4;  case 87: return Pad5;
                case 88: return Pad6;  case 89: return Pad7;  case 91: return Pad8;
                case 92: return Pad9;  case 65: return PadDot; case 69: return PadPlus;
                case 78: return PadMinus; case 67: return PadMultiply; case 75: return PadDivide;
                case 76: return PadEnter; case 81: return PadEquals; case 71: return PadClear;
                default: return -1;
            }
        }

        int modsFromFlags (CGEventFlags f)
        {
            int m = 0;
            if (f & kCGEventFlagMaskCommand)   m |= kCmd;
            if (f & kCGEventFlagMaskShift)     m |= kShift;
            if (f & kCGEventFlagMaskAlternate) m |= kOpt;
            if (f & kCGEventFlagMaskControl)   m |= kCtrl;
            return m;
        }

        CGEventFlags flagsFromMods (int m)
        {
            CGEventFlags f = 0;
            if (m & kCmd)   f |= kCGEventFlagMaskCommand;
            if (m & kShift) f |= kCGEventFlagMaskShift;
            if (m & kOpt)   f |= kCGEventFlagMaskAlternate;
            if (m & kCtrl)  f |= kCGEventFlagMaskControl;
            return f;
        }
    }

    struct KeyTap::Impl
    {
        KeyTap& owner;
        CFMachPortRef tap = nullptr;
        CFRunLoopSourceRef source = nullptr;

        explicit Impl (KeyTap& o) : owner (o) {}

        ~Impl()
        {
            if (source != nullptr) { CFRunLoopRemoveSource (CFRunLoopGetMain(), source, kCFRunLoopCommonModes); CFRelease (source); }
            if (tap != nullptr)    { CGEventTapEnable (tap, false); CFMachPortInvalidate (tap); CFRelease (tap); }
        }

        static CGEventRef callback (CGEventTapProxy, CGEventType type, CGEventRef e, void* ctx)
        {
            auto& self = *static_cast<Impl*> (ctx);

            // 処理が遅いと macOS にタップを止められるので、すぐ付け直す
            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
            {
                if (self.tap != nullptr) CGEventTapEnable (self.tap, true);
                return e;
            }
            if (type != kCGEventKeyDown && type != kCGEventKeyUp) return e;
            if (CGEventGetIntegerValueField (e, kCGEventSourceUserData) == kMagic) return e;

            const int vk   = (int) CGEventGetIntegerValueField (e, kCGKeyboardEventKeycode);
            const int mods = modsFromFlags (CGEventGetFlags (e));
            const bool down = type == kCGEventKeyDown;
            const int pad  = padFromKeyCode (vk);

            if (self.owner.learning && pad < 0)
            {
                if (down && self.owner.onLearnKey && self.owner.onLearnKey (vk, mods)) return nullptr;
                return down ? e : nullptr; // 覚えさせたキーを離した分も握りつぶす
            }

            // 修飾キーと一緒のテンキーはそのまま通す（⌘+テンキーなど、他の使い道を残す）
            if (pad < 0 || mods != 0 || ! self.owner.onPadKey) return e;

            const bool repeat = CGEventGetIntegerValueField (e, kCGKeyboardEventAutorepeat) != 0;
            return self.owner.onPadKey (pad, down, repeat) ? nullptr : e;
        }

        bool start()
        {
            if (tap != nullptr) return true;
            const CGEventMask mask = CGEventMaskBit (kCGEventKeyDown) | CGEventMaskBit (kCGEventKeyUp);
            tap = CGEventTapCreate (kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                                    mask, &Impl::callback, this);
            if (tap == nullptr) return false;
            source = CFMachPortCreateRunLoopSource (kCFAllocatorDefault, tap, 0);
            CFRunLoopAddSource (CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
            CGEventTapEnable (tap, true);
            return true;
        }
    };

    KeyTap::KeyTap() : impl (std::make_unique<Impl> (*this)) {}
    KeyTap::~KeyTap() = default;

    bool KeyTap::start()            { return impl->start(); }
    bool KeyTap::isRunning() const  { return impl->tap != nullptr && CGEventTapIsEnabled (impl->tap); }

    bool KeyTap::hasPermission() { return AXIsProcessTrusted(); }

    void KeyTap::requestPermission()
    {
        NSDictionary* opts = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @YES };
        AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) opts);
    }

    void KeyTap::openPermissionSettings()
    {
        [[NSWorkspace sharedWorkspace] openURL:
            [NSURL URLWithString: @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
    }

    static bool isLogic (NSRunningApplication* app)
    {
        return app != nil && app.bundleIdentifier != nil && [app.bundleIdentifier hasPrefix: @"com.apple.logic"];
    }

    bool KeyTap::isLogicFrontmost() { return isLogic ([[NSWorkspace sharedWorkspace] frontmostApplication]); }

    bool KeyTap::isLogicRunning()
    {
        for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
            if (isLogic (app)) return true;
        return false;
    }

    void KeyTap::sendKey (int keyCode, int mods)
    {
        // 実際に押されている修飾キーが混ざらないよう、独立したイベントソースから送る
        CGEventSourceRef src = CGEventSourceCreate (kCGEventSourceStatePrivate);
        for (bool down : { true, false })
        {
            CGEventRef ev = CGEventCreateKeyboardEvent (src, (CGKeyCode) keyCode, down);
            if (ev == nullptr) continue;
            CGEventSetFlags (ev, flagsFromMods (mods));
            CGEventSetIntegerValueField (ev, kCGEventSourceUserData, kMagic);
            CGEventPost (kCGHIDEventTap, ev);
            CFRelease (ev);
        }
        if (src != nullptr) CFRelease (src);
    }
}
