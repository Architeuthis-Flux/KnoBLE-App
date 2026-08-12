#pragma once

#include "app/Config.h"
#include "engine/ScrollEngine.h"

#include <QObject>
#include <memory>

#ifdef Q_OS_MACOS
class MacTray;
class KnobHidChannel;
#else
#include <QSystemTrayIcon>
class QAction;
class QMenu;
#endif

class SettingsWindow;

// Menu-bar presence and app wiring: owns the engine and the settings window,
// marshals engine callbacks onto the GUI thread. On macOS the tray itself is
// native AppKit (see MacTray.h for why); elsewhere it's QSystemTrayIcon.
class TrayApp : public QObject {
    Q_OBJECT

public:
    TrayApp();
    ~TrayApp() override;

private slots:
    void onDevicePresence(bool present);
    void onPermissionMissing();
    void onRawCounts(bool active);

private:
    void startEngine();
    void openSettings();
    void setEnabled(bool on);
    void showStatus(const QString &text);
    void showPermissionAction(bool visible);

    Config config_;
    ScrollEngineSettings settings_;
    std::unique_ptr<ScrollEngine> engine_;
    SettingsWindow *settingsWindow_ = nullptr;
    bool deviceConnected_ = false;
    bool rawCounts_ = false;
    void refreshStatusLine();

#ifdef Q_OS_MACOS
    std::unique_ptr<MacTray> tray_;
    std::unique_ptr<KnobHidChannel> channel_;
#else
    void buildMenu();
    QIcon makeIcon(bool connected) const;
    QSystemTrayIcon tray_;
    QMenu *menu_ = nullptr;
    QAction *statusAction_ = nullptr;
    QAction *enabledAction_ = nullptr;
    QAction *permissionAction_ = nullptr;
#endif
};
