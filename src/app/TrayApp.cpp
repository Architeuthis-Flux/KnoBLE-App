#include "app/TrayApp.h"
#include "app/SettingsWindow.h"

#ifdef Q_OS_MACOS
#include "app/mac/KnobHidChannel.h"
#include "app/mac/MacTray.h"
#include "engine/mac/MacScrollEngine.h"
#else
#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#endif

#include <QApplication>

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
    channel_ = std::make_unique<KnobHidChannel>();
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
        engine_->rawCountsActive = [this](bool active) {
            QMetaObject::invokeMethod(this, "onRawCounts", Qt::QueuedConnection,
                                      Q_ARG(bool, active));
        };
    }

#ifdef Q_OS_MACOS
    MacTray::Callbacks callbacks;
    callbacks.toggleEnabled = [this](bool on) { setEnabled(on); };
    callbacks.openSettings = [this] { openSettings(); };
    callbacks.grantPermission = [this] { startEngine(); };
    callbacks.quit = [] { QApplication::quit(); };
    tray_ = std::make_unique<MacTray>(settings_.enabled, std::move(callbacks));
#else
    buildMenu();
    tray_.setIcon(makeIcon(false));
    tray_.setToolTip(tr("Knob"));
    tray_.show();
#endif

    startEngine();
}

TrayApp::~TrayApp() {
    if (engine_) {
        engine_->stop();
    }
}

void TrayApp::setEnabled(bool on) {
    settings_.enabled = on;
    config_.save(settings_);
    if (engine_) {
        engine_->applySettings(settings_);
    }
}

void TrayApp::showStatus(const QString &text) {
#ifdef Q_OS_MACOS
    tray_->setStatusText(text);
#else
    statusAction_->setText(text);
#endif
}

void TrayApp::showPermissionAction(bool visible) {
#ifdef Q_OS_MACOS
    tray_->setPermissionItemVisible(visible);
#else
    permissionAction_->setVisible(visible);
#endif
}

void TrayApp::startEngine() {
    if (!engine_) {
        showStatus(tr("No engine for this OS yet"));
        return;
    }
    if (engine_->start()) {
        showPermissionAction(false);
    }
}

void TrayApp::openSettings() {
    if (!settingsWindow_) {
#ifdef Q_OS_MACOS
        settingsWindow_ = new SettingsWindow(settings_, channel_.get());
#else
        settingsWindow_ = new SettingsWindow(settings_, nullptr);
#endif
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
    settingsWindow_->setDeviceConnected(deviceConnected_);
    settingsWindow_->setRawCountsActive(rawCounts_);
    settingsWindow_->show();
    settingsWindow_->raise();
    settingsWindow_->activateWindow();
}

void TrayApp::onDevicePresence(bool present) {
    deviceConnected_ = present;
    if (settingsWindow_) {
        settingsWindow_->setDeviceConnected(present);
    }
    refreshStatusLine();
#ifdef Q_OS_MACOS
    tray_->setConnected(present);
#else
    tray_.setIcon(makeIcon(present));
#endif
}

void TrayApp::onRawCounts(bool active) {
    rawCounts_ = active;
    if (settingsWindow_) {
        settingsWindow_->setRawCountsActive(active);
    }
    refreshStatusLine();
}

void TrayApp::refreshStatusLine() {
    QString status = deviceConnected_ ? tr("Knob: connected") : tr("Knob: not connected");
    if (!rawCounts_) {
        status += tr(" — grant Input Monitoring for exact counts");
    }
    showStatus(status);
}

void TrayApp::onPermissionMissing() {
    showStatus(tr("Knob: needs Accessibility permission"));
    showPermissionAction(true);
#ifndef Q_OS_MACOS
    tray_.showMessage(
        tr("Knob needs a permission"),
        tr("Grant the required input permission, then retry from the menu."),
        QSystemTrayIcon::Warning);
#endif
}

#ifndef Q_OS_MACOS

void TrayApp::buildMenu() {
    menu_ = new QMenu;

    statusAction_ = menu_->addAction(tr("Knob: not connected"));
    statusAction_->setEnabled(false);

    permissionAction_ = menu_->addAction(tr("Grant permission…"));
    permissionAction_->setVisible(false);
    connect(permissionAction_, &QAction::triggered, this, [this] { startEngine(); });

    menu_->addSeparator();

    enabledAction_ = menu_->addAction(tr("Smooth scrolling"));
    enabledAction_->setCheckable(true);
    enabledAction_->setChecked(settings_.enabled);
    connect(enabledAction_, &QAction::toggled, this, [this](bool on) { setEnabled(on); });

    auto *settingsAction = menu_->addAction(tr("Settings…"));
    connect(settingsAction, &QAction::triggered, this, [this] { openSettings(); });

    menu_->addSeparator();

    auto *quitAction = menu_->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    tray_.setContextMenu(menu_);
}

// Simple template-style glyph: a knob (circle) with an index notch.
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
        p.drawLine(QPointF(18, 6), QPointF(18, 13));
        if (connected) {
            p.setBrush(Qt::black);
            p.drawEllipse(QRectF(15, 19, 6, 6));
        }
    }
    pm.setDevicePixelRatio(2.0);
    QIcon icon(pm);
    icon.setIsMask(true);
    return icon;
}

#endif // !Q_OS_MACOS
