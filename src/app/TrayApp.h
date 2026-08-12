#pragma once

#include "app/Config.h"
#include "engine/ScrollEngine.h"

#include <QObject>
#include <QSystemTrayIcon>
#include <memory>

class QAction;
class QMenu;
class SettingsWindow;

// Menu-bar presence and app wiring: owns the engine and the settings window,
// marshals engine callbacks onto the GUI thread.
class TrayApp : public QObject {
    Q_OBJECT

public:
    TrayApp();
    ~TrayApp() override;

private slots:
    void onDevicePresence(bool present);
    void onPermissionMissing();

private:
    void buildMenu();
    void startEngine();
    void openSettings();
    QIcon makeIcon(bool connected) const;

    Config config_;
    ScrollEngineSettings settings_;
    std::unique_ptr<ScrollEngine> engine_;
    QSystemTrayIcon tray_;
    QMenu *menu_ = nullptr;
    QAction *statusAction_ = nullptr;
    QAction *enabledAction_ = nullptr;
    QAction *permissionAction_ = nullptr;
    SettingsWindow *settingsWindow_ = nullptr;
    bool deviceConnected_ = false;
};
