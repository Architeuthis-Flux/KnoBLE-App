#pragma once

#include <QString>
#include <functional>
#include <memory>

// Native NSStatusItem menu-bar presence. Qt's QSystemTrayIcon crashes on
// current macOS (its QStatusItemDelegate reads -[NSEvent clickCount] during
// AppKit's scene-based status item setup and the assertion aborts), so the
// tray is AppKit; everything else stays Qt. Menu callbacks arrive on the
// AppKit main thread, which is Qt's GUI thread — direct calls are safe.
class MacTray {
public:
    struct Callbacks {
        std::function<void(bool)> toggleEnabled;
        std::function<void()> openSettings;
        std::function<void()> grantPermission;
        std::function<void()> quit;
    };

    MacTray(bool enabledChecked, Callbacks callbacks);
    ~MacTray();

    void setStatusText(const QString &text);
    void setPermissionItemVisible(bool visible);
    void setConnected(bool connected);

    struct Impl; // opaque; named by the ObjC menu target, so not private

private:
    std::unique_ptr<Impl> impl_;
};
