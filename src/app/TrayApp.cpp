#include "app/TrayApp.h"
#include "app/SettingsWindow.h"

#ifdef Q_OS_MACOS
#include "engine/mac/MacScrollEngine.h"
#endif

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

namespace {

// Devices whose wheel we smooth. KnoBLE ships with ZMK's default VID/PID;
// wired KNOBs can be added here once their wheel mode matters.
std::vector<DeviceFilter> knobDevices() {
    return {
        {0x1d50, 0x615e}, // KnoBLE (ZMK / OpenMoko)
    };
}

} // namespace

TrayApp::TrayApp() {
    settings_ = config_.load();

#ifdef Q_OS_MACOS
    engine_ = std::make_unique<MacScrollEngine>(knobDevices());
#endif

    if (engine_) {
        engine_->applySettings(settings_);
        // Engine callbacks arrive on the engine thread; hop to the GUI thread.
        engine_->deviceConnected = [this](bool present) {
            QMetaObject::invokeMethod(this, "onDevicePresence", Qt::QueuedConnection,
                                      Q_ARG(bool, present));
        };
        engine_->permissionMissing = [this] {
            QMetaObject::invokeMethod(this, "onPermissionMissing", Qt::QueuedConnection);
        };
    }

    buildMenu();
    tray_.setIcon(makeIcon(false));
    tray_.setToolTip(tr("Knob"));
    tray_.show();

    startEngine();
}

TrayApp::~TrayApp() {
    if (engine_) {
        engine_->stop();
    }
}

void TrayApp::buildMenu() {
    menu_ = new QMenu;

    statusAction_ = menu_->addAction(tr("Knob: not connected"));
    statusAction_->setEnabled(false);

    permissionAction_ = menu_->addAction(tr("Grant Accessibility permission…"));
    permissionAction_->setVisible(false);
    connect(permissionAction_, &QAction::triggered, this, [this] { startEngine(); });

    menu_->addSeparator();

    enabledAction_ = menu_->addAction(tr("Smooth scrolling"));
    enabledAction_->setCheckable(true);
    enabledAction_->setChecked(settings_.enabled);
    connect(enabledAction_, &QAction::toggled, this, [this](bool on) {
        settings_.enabled = on;
        config_.save(settings_);
        if (engine_) {
            engine_->applySettings(settings_);
        }
    });

    auto *settingsAction = menu_->addAction(tr("Settings…"));
    connect(settingsAction, &QAction::triggered, this, [this] { openSettings(); });

    menu_->addSeparator();

    auto *quitAction = menu_->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    tray_.setContextMenu(menu_);
}

void TrayApp::startEngine() {
    if (!engine_) {
        statusAction_->setText(tr("No engine for this OS yet"));
        return;
    }
    if (engine_->start()) {
        permissionAction_->setVisible(false);
    }
}

void TrayApp::openSettings() {
    if (!settingsWindow_) {
        settingsWindow_ = new SettingsWindow(settings_);
        settingsWindow_->setAttribute(Qt::WA_DeleteOnClose);
        connect(settingsWindow_, &QObject::destroyed, this,
                [this] { settingsWindow_ = nullptr; });
        settingsWindow_->onChanged = [this](const ScrollEngineSettings &s) {
            const bool enabled = settings_.enabled;
            settings_ = s;
            settings_.enabled = enabled; // toggled from the tray, not the panel
            config_.save(settings_);
            if (engine_) {
                engine_->applySettings(settings_);
            }
        };
    }
    settingsWindow_->show();
    settingsWindow_->raise();
    settingsWindow_->activateWindow();
}

void TrayApp::onDevicePresence(bool present) {
    deviceConnected_ = present;
    statusAction_->setText(present ? tr("Knob: connected") : tr("Knob: not connected"));
    tray_.setIcon(makeIcon(present));
}

void TrayApp::onPermissionMissing() {
    statusAction_->setText(tr("Knob: needs Accessibility permission"));
    permissionAction_->setVisible(true);
    tray_.showMessage(
        tr("Knob needs a permission"),
        tr("Grant Accessibility to Knob in System Settings → Privacy & Security, then choose "
           "\"Grant Accessibility permission…\" from the menu."),
        QSystemTrayIcon::Warning);
}

// Simple template-style glyph: a knob (circle) with an index notch. Drawn in
// code so v0.1 needs no asset pipeline; menu bar tints template pixmaps.
QIcon TrayApp::makeIcon(bool connected) const {
    QPixmap pm(36, 36);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(Qt::black, 3);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(4, 4, 28, 28));
        // index notch at 12 o'clock
        p.drawLine(QPointF(18, 6), QPointF(18, 13));
        if (connected) {
            p.setBrush(Qt::black);
            p.drawEllipse(QRectF(15, 19, 6, 6));
        }
    }
    pm.setDevicePixelRatio(2.0);
    QIcon icon(pm);
    icon.setIsMask(true); // template icon: adapts to light/dark menu bar
    return icon;
}
