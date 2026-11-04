// ウィンドウを動かす部分（全画面・2 画面・置き場所・リンクを 2 画面で開く）。
// アクセシビリティ（AX）でほかのアプリのウィンドウを動かし、macOS の全画面の左右分割を使う。
// キーの横取り（KeyTap_mac.mm）とは分けてある。
#include "KeyTap.h"
#include "Windows.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cmath>
#include <map>
#include <vector>

namespace numdeck
{
    using win::ns;

    // 動きの記録（~/Library/Logs/Num Deck.log）。うまく並ばなかったときに、どこで止まったかを後から見るため
    static void wlog (NSString* fmt, ...)
    {
        va_list ap;
        va_start (ap, fmt);
        NSString* msg = [[NSString alloc] initWithFormat: fmt arguments: ap];
        va_end (ap);
        NSString* path = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Logs/Num Deck.log"];
        NSString* line = [NSString stringWithFormat: @"%@  %@\n", [NSDate date], msg];
        NSFileHandle* h = [NSFileHandle fileHandleForWritingAtPath: path];
        if (h == nil) { [line writeToFile: path atomically: NO encoding: NSUTF8StringEncoding error: nil]; }
        else { [h seekToEndOfFile]; [h writeData: [line dataUsingEncoding: NSUTF8StringEncoding]]; [h closeFile]; }
       #if ! __has_feature(objc_arc)
        [msg release];
       #endif
    }

    namespace
    {
        // マウスのある画面の、メニューバーと Dock を除いた範囲（アクセシビリティの座標＝いちばん左上が原点）
        CGRect visibleAreaForAX()
        {
            const NSPoint m = [NSEvent mouseLocation];
            NSScreen* scr = [NSScreen mainScreen];
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (m, s.frame)) { scr = s; break; }
            const NSRect v = scr.visibleFrame;
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            return CGRectMake (v.origin.x, primaryH - (v.origin.y + v.size.height), v.size.width, v.size.height);
        }

        void placeWindow (AXUIElementRef win, CGRect r)
        {
            CGPoint p = r.origin;
            CGSize s = r.size;
            AXValueRef pv = AXValueCreate ((AXValueType) kAXValueCGPointType, &p);
            AXValueRef sv = AXValueCreate ((AXValueType) kAXValueCGSizeType, &s);
            AXUIElementSetAttributeValue (win, kAXPositionAttribute, pv);
            AXUIElementSetAttributeValue (win, kAXSizeAttribute, sv);
            AXUIElementSetAttributeValue (win, kAXPositionAttribute, pv); // 大きさを変えるとずれるアプリがあるので、もう一度
            CFRelease (pv);
            CFRelease (sv);
        }

        pid_t runningPid (NSString* bundleId)
        {
            for (NSRunningApplication* a in [NSRunningApplication runningApplicationsWithBundleIdentifier: bundleId])
                if (! a.terminated) return a.processIdentifier;
            return 0;
        }

        // そのアプリの前面のウィンドウ（呼んだ側で CFRelease）
        AXUIElementRef copyFrontWindow (NSString* bundleId)
        {
            const pid_t pid = runningPid (bundleId);
            if (pid == 0) return nullptr;
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef w = nullptr;
            AXUIElementCopyAttributeValue (app, kAXFocusedWindowAttribute, &w);
            if (w == nullptr) AXUIElementCopyAttributeValue (app, kAXMainWindowAttribute, &w);
            CFRelease (app);
            return (AXUIElementRef) w;
        }

        bool axBool (AXUIElementRef e, CFStringRef attr)
        {
            CFTypeRef v = nullptr;
            AXUIElementCopyAttributeValue (e, attr, &v);
            const bool b = v != nullptr && CFGetTypeID (v) == CFBooleanGetTypeID() && CFBooleanGetValue ((CFBooleanRef) v);
            if (v != nullptr) CFRelease (v);
            return b;
        }

        // cond が true になるまで 0.1 秒ごとに確かめて待つ（最大 timeoutMs）。then には間に合ったかを渡す。
        // 決め打ちの待ち時間だと、Mac が忙しいときに全画面の切り替えが終わる前に次へ進んでずれるため
        void waitFor (bool (^cond) (void), int timeoutMs, void (^then) (bool))
        {
            if (cond()) { then (true); return; }
            if (timeoutMs <= 0) { then (false); return; }
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                waitFor (cond, timeoutMs - 100, then);
            });
        }

        void later (int ms, void (^block) (void))
        {
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) ms * (int64_t) NSEC_PER_MSEC), dispatch_get_main_queue(), block);
        }

        bool isFullScreen (AXUIElementRef w) { return axBool (w, CFSTR ("AXFullScreen")); }

        // 全画面に入れる・出す。切り替えのアニメーション中に頼むと macOS に無視されるので、
        // 本当に切り替わったかを確かめ、だめなら少し待って頼み直す（最大 tries 回）
        void setFullScreenSurely (AXUIElementRef w, bool on, int tries, void (^then) (bool))
        {
            AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), on ? kCFBooleanTrue : kCFBooleanFalse);
            waitFor (^{ return isFullScreen (w) == on; }, 1500, ^(bool ok) {
                if (ok || tries <= 1) { if (then != nil) then (ok); return; }
                later (600, ^{ setFullScreenSurely (w, on, tries - 1, then); });
            });
        }

        // 全画面を抜けきって（アニメーションも終わって）から、もう一度全画面に入れる
        void refullScreen (AXUIElementRef w)
        {
            AXUIElementRef win = (AXUIElementRef) CFRetain (w);
            setFullScreenSurely (win, false, 3, ^(bool) {
                later (700, ^{
                    setFullScreenSurely (win, true, 5, ^(bool) { CFRelease (win); });
                });
            });
        }

        // アプリを前に出す（activateWithOptions は後ろにいる Num Deck からだと断られることがあるので、開き直す）
        void bringAppForward (pid_t pid)
        {
            if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                if (a.bundleURL != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: a.bundleURL
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
        }

        // そのアプリの、今キーボードが向いているウィンドウ（呼んだ側で CFRelease）
        AXUIElementRef copyFocusedWindowOf (pid_t pid)
        {
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef w = nullptr;
            AXUIElementCopyAttributeValue (app, kAXFocusedWindowAttribute, &w);
            CFRelease (app);
            return (AXUIElementRef) w;
        }

        // win を、そのアプリの前面（キーボードが向く）ウィンドウにする
        void focusWindow (AXUIElementRef win)
        {
            AXUIElementSetAttributeValue (win, kAXMainAttribute, kCFBooleanTrue);
            AXUIElementPerformAction (win, kAXRaiseAction);
        }

        // pid のアプリが前面で、そのキーボードが win を向いているか
        bool isFocused (pid_t pid, AXUIElementRef win)
        {
            NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if (front == nil || front.processIdentifier != pid) return false;
            AXUIElementRef f = copyFocusedWindowOf (pid);
            const bool ok = f != nullptr && CFEqual (f, win);
            if (f != nullptr) CFRelease (f);
            return ok;
        }

        // 全画面（ほかと左右分割中も含む）なら解除し、しまってあれば出す。解除したら true（落ち着くまで少し待つ）
        bool bringOutOfFullScreen (AXUIElementRef w)
        {
            if (axBool (w, kAXMinimizedAttribute))
                AXUIElementSetAttributeValue (w, kAXMinimizedAttribute, kCFBooleanFalse);
            if (! axBool (w, CFSTR ("AXFullScreen"))) return false;
            AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            return true;
        }

        // before と違うウィンドウが前に出るまで待って（アプリの起動を待つこともあるので最大 20 秒）、
        // 全画面なら解除してから r に置き、then（置いたウィンドウ、なければ nullptr）。before は最後に CFRelease する
        void waitAndPlace (NSString* bundleId, AXUIElementRef before, CGRect r, int tries, void (^then) (AXUIElementRef))
        {
            AXUIElementRef w = copyFrontWindow (bundleId);
            const bool ready = w != nullptr && (before == nullptr || ! CFEqual (w, before));
            if (ready && tries < 190 && bringOutOfFullScreen (w))
            {
                // 全画面を抜けきってから置き直す（同じウィンドウなので before は外す）
                if (before != nullptr) CFRelease (before);
                waitFor (^{ return ! isFullScreen (w); }, 4000, ^(bool) {
                    CFRelease (w);
                    later (300, ^{ waitAndPlace (bundleId, nullptr, r, tries + 1, then); });
                });
                return;
            }
            if (! ready && tries < 200)
            {
                if (w != nullptr) CFRelease (w);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    waitAndPlace (bundleId, before, r, tries + 1, then);
                });
                return;
            }
            if (ready) placeWindow (w, r);
            wlog (@"place %@: ready=%d tries=%d", bundleId, ready, tries);
            if (before != nullptr) CFRelease (before);
            if (then != nil) then (ready ? w : nullptr);
            if (w != nullptr) CFRelease (w);
        }

        CFStringRef axTitle (AXUIElementRef e)
        {
            CFTypeRef t = nullptr;
            AXUIElementCopyAttributeValue (e, kAXTitleAttribute, &t);
            return (CFStringRef) t;
        }

        // メニューの中から、titles のどれかの名前の項目（呼んだ側で CFRelease）
        AXUIElementRef copyChildItem (AXUIElementRef parent, NSArray<NSString*>* titles)
        {
            CFTypeRef kids = nullptr;
            AXUIElementCopyAttributeValue (parent, kAXChildrenAttribute, &kids);
            if (kids == nullptr) return nullptr;
            AXUIElementRef found = nullptr;
            for (id k in (__bridge NSArray*) kids)
            {
                AXUIElementRef e = (__bridge AXUIElementRef) k;
                CFStringRef t = axTitle (e);
                const bool hit = t != nullptr && [titles containsObject: (__bridge NSString*) t];
                if (t != nullptr) CFRelease (t);
                if (hit) { found = (AXUIElementRef) CFRetain (e); break; }
                // メニューバーの項目・サブメニューは、子の AXMenu を 1 段下りて探す
                CFStringRef role = nullptr;
                AXUIElementCopyAttributeValue (e, kAXRoleAttribute, (CFTypeRef*) &role);
                const bool isMenu = role != nullptr && CFEqual (role, kAXMenuRole);
                if (role != nullptr) CFRelease (role);
                if (isMenu && (found = copyChildItem (e, titles)) != nullptr) break;
            }
            CFRelease (kids);
            return found;
        }

        // path のアプリの前面のウィンドウが、全画面の左右分割（全画面なのに画面の幅より狭い）に入っていれば、
        // 分割を解いてそのウィンドウだけの全画面にする。した場合 true
        bool unsplitToFullScreen (NSString* path)
        {
            NSRunningApplication* app = nil;
            for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
                if (a.bundleURL != nil && [[a.bundleURL.path stringByStandardizingPath] isEqualToString: path]) { app = a; break; }
            if (app == nil || app.bundleIdentifier == nil) return false;
            AXUIElementRef win = copyFrontWindow (app.bundleIdentifier);
            if (win == nullptr) return false;
            if (! axBool (win, CFSTR ("AXFullScreen"))) { CFRelease (win); return false; }

            CGPoint pos {}; CGSize size {};
            CFTypeRef pv = nullptr, sv = nullptr;
            AXUIElementCopyAttributeValue (win, kAXPositionAttribute, &pv);
            AXUIElementCopyAttributeValue (win, kAXSizeAttribute, &sv);
            if (pv != nullptr) { AXValueGetValue ((AXValueRef) pv, (AXValueType) kAXValueCGPointType, &pos); CFRelease (pv); }
            if (sv != nullptr) { AXValueGetValue ((AXValueRef) sv, (AXValueType) kAXValueCGSizeType, &size); CFRelease (sv); }

            // ウィンドウがある画面の幅と比べる（画面の座標は左下が原点なので変換）
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            const NSPoint probe = NSMakePoint (pos.x + 2, primaryH - pos.y - 2);
            CGFloat screenW = 0;
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (probe, s.frame)) screenW = s.frame.size.width;
            if (screenW <= 0 || size.width > screenW * 0.9) { CFRelease (win); return false; }

            [[NSWorkspace sharedWorkspace] openApplicationAtURL: app.bundleURL
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            refullScreen (win);
            CFRelease (win);
            return true;
        }

        // app の前面のウィンドウが全画面なら、全画面を解いて、落ち着いてから隠して backURL のアプリへ。した場合 true
        bool leaveFullScreenThenHide (NSRunningApplication* app, NSURL* backURL)
        {
            if (app.bundleIdentifier == nil) return false;
            AXUIElementRef win = copyFrontWindow (app.bundleIdentifier);
            if (win == nullptr) return false;
            const bool fs = axBool (win, CFSTR ("AXFullScreen"));
            if (! fs) { CFRelease (win); return false; }
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            const pid_t pid = app.processIdentifier;
            NSURL* back = [backURL copy];
            waitFor (^{ return ! isFullScreen (win); }, 4000, ^(bool) { CFRelease (win); later (300, ^{
                if (back != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: back
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
                if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                    [a hide];
            }); });
            return true;
        }

        // アプリのメニュー「ウィンドウ」→「フルスクリーンのタイル表示」→「画面左側」を押す
        bool pressTileLeft (pid_t pid)
        {
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef bar = nullptr;
            AXUIElementCopyAttributeValue (app, kAXMenuBarAttribute, &bar);
            CFRelease (app);
            if (bar == nullptr) return false;
            bool ok = false;
            if (AXUIElementRef win = copyChildItem ((AXUIElementRef) bar, @[ @"ウィンドウ", @"ウインドウ", @"Window" ]))
            {
                if (AXUIElementRef tile = copyChildItem (win, @[ @"フルスクリーンのタイル表示", @"Full Screen Tile" ]))
                {
                    if (AXUIElementRef left = copyChildItem (tile, @[ @"画面左側", @"Left of Screen" ]))
                    {
                        ok = AXUIElementPerformAction (left, kAXPressAction) == kAXErrorSuccess;
                        CFRelease (left);
                    }
                    CFRelease (tile);
                }
                CFRelease (win);
            }
            CFRelease (bar);
            return ok;
        }

        void clickAt (CGPoint p)
        {
            CGEventRef cur = CGEventCreate (nullptr);
            const CGPoint back = CGEventGetLocation (cur);
            CFRelease (cur);
            for (CGEventType t : { kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp })
            {
                CGEventRef e = CGEventCreateMouseEvent (nullptr, t, p, kCGMouseButtonLeft);
                CGEventPost (kCGHIDEventTap, e);
                CFRelease (e);
                usleep (60000);
            }
            CGEventRef mv = CGEventCreateMouseEvent (nullptr, kCGEventMouseMoved, back, kCGMouseButtonLeft);
            CGEventPost (kCGHIDEventTap, mv);
            CFRelease (mv);
        }

        // 左右に並べた 2 つを、macOS の全画面の左右分割にする。
        // 左のウィンドウを「画面左側」にすると、右に置く相手を選ぶ画面が出る。
        // そこでは各ウィンドウが元の位置を縮めた所に並ぶので、右半分に置いた相手の所（右半分の真ん中）をクリックする
        // 相手を選ぶ画面には、今の画面にあるほかのアプリのウィンドウも並ぶ。違うものを選ばないよう、
        // 左右の 2 つのアプリ以外（Num Deck も）をいったん隠し、分割できたら元に戻す
        void makeFullScreenSplit (pid_t leftPid, AXUIElementRef leftWin, pid_t rightPid, AXUIElementRef rightWin, CGRect area)
        {
            NSMutableArray<NSRunningApplication*>* hidden = [NSMutableArray array];
            for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
            {
                if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.hidden) continue;
                if (a.processIdentifier == leftPid || a.processIdentifier == rightPid) continue;
                if ([a hide]) [hidden addObject: a];
            }
           #if ! __has_feature(objc_arc)
            [hidden retain];
           #endif
            AXUIElementRef win = (AXUIElementRef) CFRetain (leftWin);
            AXUIElementRef rwin = (AXUIElementRef) CFRetain (rightWin);
            auto finish = ^(const char* message) {
                if (message != nullptr) KeyTap::showHUD (message);
                for (NSRunningApplication* a in hidden) [a unhide];
               #if ! __has_feature(objc_arc)
                [hidden release];
               #endif
                CFRelease (win);
                CFRelease (rwin);
            };

            // 1. 左のウィンドウのアプリを前に出し、キーボードが左のウィンドウを向くまで待つ
            //    （向く前に「画面左側」を押すと、同じアプリの別のウィンドウ（研修の画面など）が分割されてしまう）
            bringAppForward (leftPid);
            focusWindow (win);
            wlog (@"split: start left=%d right=%d hidden=%lu", leftPid, rightPid, (unsigned long) hidden.count);
            waitFor (^{ if (! isFocused (leftPid, win)) focusWindow (win); return isFocused (leftPid, win); }, 3000, ^(bool focused) {
                wlog (@"split: left focused=%d", focused);
                if (! focused) { finish ("うまく並べられませんでした（もう一度押してください）"); return; }
                // 2. 「ウィンドウ」→「フルスクリーンのタイル表示」→「画面左側」
                const bool pressed = pressTileLeft (leftPid);
                wlog (@"split: tile-left pressed=%d", pressed);
                if (! pressed) { finish ("この画面では 2 分割にできませんでした"); return; }
                // 3. 左が全画面に入る（＝右に置く相手を選ぶ画面が出た）のを待ってから、右半分の真ん中をクリック
                waitFor (^{ return isFullScreen (win); }, 4000, ^(bool entered) {
                    wlog (@"split: left entered=%d", entered);
                    if (! entered) { finish ("うまく並べられませんでした（もう一度押してください）"); return; }
                    later (600, ^{
                        clickAt (CGPointMake (area.origin.x + area.size.width * 0.875, area.origin.y + area.size.height * 0.5));
                        // 4. 右に入ったか確かめる。入っていなければ、選ぶ画面で自分で選んでもらう
                        waitFor (^{ return isFullScreen (rwin); }, 3000, ^(bool ok) {
                            wlog (@"split: right entered=%d", ok);
                            finish (ok ? nullptr : "右に置くウィンドウをクリックしてください");
                        });
                    });
                });
            });
        }

        // Chrome を引数付きで開く（起動中なら、起動中の Chrome に渡される）
        void launchChrome (const std::string& profileDir, NSArray<NSString*>* args)
        {
            NSURL* chrome = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: @"com.google.Chrome"];
            if (chrome == nil) return;
            NSMutableArray<NSString*>* a = [NSMutableArray array];
            if (! profileDir.empty()) [a addObject: [@"--profile-directory=" stringByAppendingString: ns (profileDir)]];
            [a addObjectsFromArray: args];
            NSWorkspaceOpenConfiguration* c = [NSWorkspaceOpenConfiguration configuration];
            c.createsNewApplicationInstance = YES;
            c.arguments = a;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: chrome configuration: c completionHandler: nil];
        }
    }

    namespace
    {
        void getFrame (AXUIElementRef w, CGPoint& pos, CGSize& size)
        {
            CFTypeRef pv = nullptr, sv = nullptr;
            AXUIElementCopyAttributeValue (w, kAXPositionAttribute, &pv);
            AXUIElementCopyAttributeValue (w, kAXSizeAttribute, &sv);
            if (pv != nullptr) { AXValueGetValue ((AXValueRef) pv, (AXValueType) kAXValueCGPointType, &pos); CFRelease (pv); }
            if (sv != nullptr) { AXValueGetValue ((AXValueRef) sv, (AXValueType) kAXValueCGSizeType, &size); CFRelease (sv); }
        }

        // pos がある画面の幅（アクセシビリティの座標から）
        CGFloat screenWidthAt (CGPoint pos)
        {
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            const NSPoint probe = NSMakePoint (pos.x + 2, primaryH - pos.y - 2);
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (probe, s.frame)) return s.frame.size.width;
            return 0;
        }

        // w が今のデスクトップ（画面に出ている）にあるか。ウィンドウの一覧（画面に出ている分だけ）で、持ち主と位置・大きさが合うものを探す
        bool onCurrentSpace (pid_t pid, AXUIElementRef w)
        {
            CGPoint pos {}; CGSize size {};
            getFrame (w, pos, size);
            CFArrayRef list = CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
            if (list == nullptr) return false;
            bool found = false;
            for (NSDictionary* d in (__bridge NSArray*) list)
            {
                if ([d[(__bridge NSString*) kCGWindowOwnerPID] intValue] != pid) continue;
                CGRect r {};
                if (! CGRectMakeWithDictionaryRepresentation ((__bridge CFDictionaryRef) d[(__bridge NSString*) kCGWindowBounds], &r)) continue;
                if (std::abs (r.origin.x - pos.x) < 4 && std::abs (r.origin.y - pos.y) < 4
                    && std::abs (r.size.width - size.width) < 4 && std::abs (r.size.height - size.height) < 4) { found = true; break; }
            }
            CFRelease (list);
            return found;
        }

        bool inSplit (AXUIElementRef w)
        {
            if (! axBool (w, CFSTR ("AXFullScreen"))) return false;
            CGPoint pos {}; CGSize size {};
            getFrame (w, pos, size);
            const CGFloat sw = screenWidthAt (pos);
            return sw > 0 && size.width < sw * 0.9;
        }

        bool stillExists (AXUIElementRef w)
        {
            if (w == nullptr) return false;
            CFTypeRef role = nullptr;
            const bool ok = AXUIElementCopyAttributeValue (w, kAXRoleAttribute, &role) == kAXErrorSuccess;
            if (role != nullptr) CFRelease (role);
            return ok;
        }

        // l を左、r を右にして全画面の左右分割にする（どちらかが全画面なら先に解除して待つ）
        void resplit (AXUIElementRef l, pid_t lPid, AXUIElementRef r, pid_t rPid)
        {
            for (AXUIElementRef w : { l, r })
            {
                if (axBool (w, kAXMinimizedAttribute)) AXUIElementSetAttributeValue (w, kAXMinimizedAttribute, kCFBooleanFalse);
                if (isFullScreen (w)) AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            }
            AXUIElementRef lw = (AXUIElementRef) CFRetain (l);
            AXUIElementRef rw = (AXUIElementRef) CFRetain (r);
            waitFor (^{ return ! isFullScreen (lw) && ! isFullScreen (rw); }, 5000, ^(bool) {
                later (400, ^{
                    const CGRect area = visibleAreaForAX();
                    const CGFloat half = std::floor (area.size.width / 2);
                    placeWindow (lw, CGRectMake (area.origin.x, area.origin.y, half, area.size.height));
                    placeWindow (rw, CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height));
                    AXUIElementPerformAction (rw, kAXRaiseAction);
                    later (300, ^{
                        makeFullScreenSplit (lPid, lw, rPid, rw, area);
                        CFRelease (lw);
                        CFRelease (rw);
                    });
                });
            });
        }

    }

    void KeyTap::toggleFullScreenOrSplit()
    {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front == nil || front.bundleIdentifier == nil) return;
        AXUIElementRef win = copyFrontWindow (front.bundleIdentifier);
        if (win == nullptr) return;

        // 1. 左右分割の片側 → このウィンドウだけの全画面に
        if (inSplit (win))
        {
            showHUD ("全画面");
            refullScreen (win);
            CFRelease (win);
            return;
        }

        // 2. 全画面（1 枚）→ 全画面を解除するだけ（2 画面には戻さない）
        if (isFullScreen (win))
        {
            showHUD ("全画面を解除");
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            CFRelease (win);
            return;
        }

        // 3. 普通のウィンドウ → 全画面に
        showHUD ("全画面");
        AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
        CFRelease (win);
    }

    namespace
    {
        // 2 画面で開いたリンクごとの、左右のウィンドウ（もう一度押したとき、残っていれば使い回す）
        struct LinkWindows { AXUIElementRef left = nullptr, right = nullptr; pid_t leftPid = 0, rightPid = 0; };
        std::map<std::string, LinkWindows> linkWindows;

        void rememberLink (const std::string& url, AXUIElementRef l, pid_t lp, AXUIElementRef r, pid_t rp)
        {
            auto& e = linkWindows[url];
            if (e.left != nullptr) CFRelease (e.left);
            if (e.right != nullptr) CFRelease (e.right);
            e = { (AXUIElementRef) CFRetain (l), (AXUIElementRef) CFRetain (r), lp, rp };
        }
    }

    namespace
    {
        // Chrome のウィンドウの中から、host のページのタブがあるものを探し、そのタブを選んで前に出す（AppleScript）。
        // 全画面（左右分割も）の別の画面にあっても、そこへ移る。見つかったら true
        bool raiseChromeWindowWithHost (const std::string& host)
        {
            if (host.empty()) return false;
            NSString* src = [NSString stringWithFormat:
                @"with timeout of 3 seconds\n"
                 "tell application \"Google Chrome\"\n"
                 "repeat with w in windows\n"
                 "set n to count of tabs of w\n"
                 "repeat with i from 1 to n\n"
                 "if URL of tab i of w contains \"%@\" then\n"
                 "set active tab index of w to i\n"
                 "set index of w to 1\n"
                 "activate\n"
                 "return \"found\"\n"
                 "end if\n"
                 "end repeat\n"
                 "end repeat\n"
                 "return \"\"\n"
                 "end tell\n"
                 "end timeout", ns (host)];
            NSAppleScript* script = [[NSAppleScript alloc] initWithSource: src];
            NSDictionary* err = nil;
            NSAppleEventDescriptor* r = [script executeAndReturnError: &err];
            const bool found = r != nil && [[r stringValue] isEqualToString: @"found"];
           #if ! __has_feature(objc_arc)
            [script release];
           #endif
            return found;
        }

        std::string hostOf (const std::string& url)
        {
            NSURL* u = [NSURL URLWithString: ns (url)];
            return u != nil && u.host != nil ? std::string (u.host.UTF8String) : std::string();
        }
    }

    void KeyTap::openURLSplit (const std::string& url, const std::string& profileDir, const std::string& split)
    {
        NSString* chromeId = @"com.google.Chrome";
        if ([[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil) { openURL (url, profileDir); return; }

        const CGRect area = visibleAreaForAX();
        const CGFloat half = std::floor (area.size.width / 2);
        const CGRect left  = CGRectMake (area.origin.x, area.origin.y, half, area.size.height);
        const CGRect right = CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height);
        const std::string rightSide = split, key = url;

        // 左のウィンドウが決まったあと: 右を用意して、全画面の左右分割にする
        auto withLeft = ^(AXUIElementRef placedLeft, bool reuseRight) {
            AXUIElementRef leftWin = (AXUIElementRef) CFRetain (placedLeft);
            const pid_t chromePid = runningPid (chromeId);
            auto finish = ^(AXUIElementRef placedRight) {
                if (placedRight != nullptr)
                {
                    pid_t rp = 0;
                    AXUIElementGetPid (placedRight, &rp);
                    rememberLink (key, leftWin, chromePid, placedRight, rp);
                    makeFullScreenSplit (chromePid, leftWin, rp, placedRight, area);
                }
                CFRelease (leftWin);
            };
            // 前にこのリンクで使った右のウィンドウが残っていれば、それを使う
            auto it = linkWindows.find (key);
            if (reuseRight && it != linkWindows.end() && stillExists (it->second.right) && ! CFEqual (it->second.right, leftWin))
            {
                AXUIElementRef rw = (AXUIElementRef) CFRetain (it->second.right);
                const pid_t rp = it->second.rightPid;
                rememberLink (key, leftWin, chromePid, rw, rp);
                resplit (leftWin, chromePid, rw, rp);
                CFRelease (rw);
                CFRelease (leftWin);
                return;
            }
            if (rightSide == "chrome")
            {
                launchChrome (profileDir, @[ @"--new-window" ]);
                waitAndPlace (chromeId, (AXUIElementRef) CFRetain (leftWin), right, 0, finish);
                return;
            }
            NSString* path = ns (rightSide);
            NSString* bid = [NSBundle bundleWithPath: path].bundleIdentifier;
            if (bid == nil) { CFRelease (leftWin); return; }
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: [NSURL fileURLWithPath: path]
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            waitAndPlace (bid, nullptr, right, 0, finish);
        };

        // 新しいウィンドウで開く
        auto openNew = ^{
            AXUIElementRef before = copyFrontWindow (chromeId);
            launchChrome (profileDir, @[ @"--new-window", ns (key) ]);
            waitAndPlace (chromeId, before, left, 0, ^(AXUIElementRef placedLeft) {
                if (placedLeft != nullptr) withLeft (placedLeft, false);
            });
        };

        // 右がアプリ（Claude など）のとき: 先にそのアプリを前に出して、そのウィンドウがあるデスクトップに移る。
        // 左のページのウィンドウがそのデスクトップに無ければ、そこに新しく開く。
        // （2 画面の相手を選ぶ画面には、今のデスクトップにあるウィンドウしか出ないため。2 つが別々のデスクトップだと分割できない）
        auto appRight = ^(AXUIElementRef reuseLeft) {
            NSString* path = ns (rightSide);
            NSString* bid = [NSBundle bundleWithPath: path].bundleIdentifier;
            if (bid == nil) { openNew(); return; }
            AXUIElementRef cand = reuseLeft != nullptr ? (AXUIElementRef) CFRetain (reuseLeft) : nullptr;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: [NSURL fileURLWithPath: path]
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            waitAndPlace (bid, nullptr, right, 0, ^(AXUIElementRef placedRight) {
                if (placedRight == nullptr) { if (cand != nullptr) CFRelease (cand); return; }
                AXUIElementRef rw = (AXUIElementRef) CFRetain (placedRight);
                pid_t rp = 0;
                AXUIElementGetPid (rw, &rp);
                const pid_t chromePid = runningPid (chromeId);
                auto go = ^(AXUIElementRef lw) {
                    rememberLink (key, lw, chromePid, rw, rp);
                    makeFullScreenSplit (chromePid, lw, rp, rw, area);
                };
                const bool here = cand != nullptr && onCurrentSpace (chromePid, cand);
                wlog (@"app-right: right placed, reuse-left here=%d", here);
                if (here)
                {
                    placeWindow (cand, left);
                    later (300, ^{ go (cand); CFRelease (cand); CFRelease (rw); });
                    return;
                }
                if (cand != nullptr) CFRelease (cand);
                AXUIElementRef before = copyFrontWindow (chromeId);
                launchChrome (profileDir, @[ @"--new-window", ns (key) ]);
                waitAndPlace (chromeId, before, left, 0, ^(AXUIElementRef placedLeft) {
                    if (placedLeft != nullptr) go (placedLeft);
                    CFRelease (rw);
                });
            });
        };

        // このページのタブがある Chrome のウィンドウがあれば、それを使い回す（Num Deck を入れ直したあとでも見つかる）。
        // そのウィンドウがもう左右分割に入っていれば、その画面へ移るだけ。普通のウィンドウなら左に置いて分割し直す
        const bool foundTab = raiseChromeWindowWithHost (hostOf (url));
        wlog (@"link split: %s found-tab=%d right=%s", url.c_str(), foundTab, split.c_str());
        if (! foundTab) { if (rightSide == "chrome") openNew(); else appRight (nullptr); return; }
        waitFor (^{ NSRunningApplication* f = [[NSWorkspace sharedWorkspace] frontmostApplication];
                    return f != nil && [f.bundleIdentifier isEqualToString: chromeId]; }, 3000, ^(bool) {
            later (500, ^{
                AXUIElementRef w = copyFocusedWindowOf (runningPid (chromeId));
                if (w == nullptr) { if (rightSide == "chrome") openNew(); else appRight (nullptr); return; }
                if (inSplit (w)) { CFRelease (w); return; }
                if (rightSide != "chrome") { appRight (w); CFRelease (w); return; }
                auto placeAndGo = ^{
                    placeWindow (w, left);
                    withLeft (w, true);
                    CFRelease (w);
                };
                if (isFullScreen (w))
                    setFullScreenSurely (w, false, 3, ^(bool) { later (500, placeAndGo); });
                else
                    placeAndGo();
            });
        });
    }

    std::vector<std::string> KeyTap::screenNames()
    {
        std::vector<std::string> names;
        for (NSScreen* s in [NSScreen screens])
        {
            NSString* n = nil;
            if (@available (macOS 10.15, *)) n = s.localizedName;
            if (n == nil) n = [NSString stringWithFormat: @"Display %lu", (unsigned long) names.size() + 1];
            names.push_back (n.UTF8String != nullptr ? n.UTF8String : "Display");
        }
        return names;
    }

    namespace
    {
        // 名前の画面の、メニューバーと Dock を除いた範囲（アクセシビリティの座標）。見つからなければマウスのある画面
        CGRect visibleAreaOfScreen (const std::string& name)
        {
            if (! name.empty())
            {
                const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
                for (NSScreen* s in [NSScreen screens])
                {
                    NSString* n = nil;
                    if (@available (macOS 10.15, *)) n = s.localizedName;
                    if (n != nil && name == n.UTF8String)
                    {
                        const NSRect v = s.visibleFrame;
                        return CGRectMake (v.origin.x, primaryH - (v.origin.y + v.size.height), v.size.width, v.size.height);
                    }
                }
            }
            return visibleAreaForAX();
        }

        CGRect regionRect (CGRect a, const std::string& region)
        {
            const CGFloat hw = std::floor (a.size.width / 2), hh = std::floor (a.size.height / 2);
            if (region == "left")   return CGRectMake (a.origin.x, a.origin.y, hw, a.size.height);
            if (region == "right")  return CGRectMake (a.origin.x + hw, a.origin.y, a.size.width - hw, a.size.height);
            if (region == "top")    return CGRectMake (a.origin.x, a.origin.y, a.size.width, hh);
            if (region == "bottom") return CGRectMake (a.origin.x, a.origin.y + hh, a.size.width, a.size.height - hh);
            return a;
        }
    }

    void KeyTap::openURLPlaced (const std::string& url, const std::string& profileDir,
                                const std::string& screen, const std::string& region)
    {
        NSString* chromeId = @"com.google.Chrome";
        if (region.empty() || [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil)
        {
            openURL (url, profileDir);
            return;
        }
        // 新しいウィンドウで開き、出てきたら指定の画面の指定の場所へ動かす
        // （全画面の左右分割のほうにいても、新しいウィンドウは普通のウィンドウとして出るので、そのまま別の画面へ動かせる）
        const CGRect r = regionRect (visibleAreaOfScreen (screen), region);

        // 前にいたウィンドウが全画面（左右分割も）なら、置いたあとそちらに戻す。
        // 新しいウィンドウを開くと、その画面が全画面の外（デスクトップ）に切り替わってしまうため
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        AXUIElementRef backWin = nullptr;
        pid_t backPid = 0;
        if (front != nil && front.bundleIdentifier != nil
            && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
        {
            backWin = copyFrontWindow (front.bundleIdentifier);
            backPid = front.processIdentifier;
            if (backWin != nullptr && ! axBool (backWin, CFSTR ("AXFullScreen"))) { CFRelease (backWin); backWin = nullptr; }
        }

        AXUIElementRef before = copyFrontWindow (chromeId);
        launchChrome (profileDir, @[ @"--new-window", ns (url) ]);
        waitAndPlace (chromeId, before, r, 0, ^(AXUIElementRef placed) {
            if (placed != nullptr) AXUIElementPerformAction (placed, kAXRaiseAction);
            if (backWin == nullptr) return;
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                bringAppForward (backPid);
                focusWindow (backWin);
                CFRelease (backWin);
            });
        });
    }

    // KeyTap_mac.mm（アプリのタイル）から使う分
    namespace win
    {
        bool unsplitToFullScreen (NSString* path) { return numdeck::unsplitToFullScreen (path); }
        bool leaveFullScreenThenHide (NSRunningApplication* app, NSURL* backURL) { return numdeck::leaveFullScreenThenHide (app, backURL); }
    }
}
