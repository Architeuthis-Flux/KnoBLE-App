#include "app/SettingsWindow.h"
#include "app/Keycodes.h"

#ifdef Q_OS_MACOS
#include "app/mac/KnobHidChannel.h"
#endif

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Firmware values as flashed (compile-time devicetree), shown in the report.
// Keys are live via the raw-HID channel; the rest goes live in a later rev.
struct FirmwareDefaults {
    static constexpr int detentsPerRev = 16;
    static constexpr int linesPerRev = 240;
    static constexpr int wheelScaleMax = 4;
    static constexpr int wheelScaleMinDiv = 5;
    static constexpr const char *slotNames[3] = {"Left key", "Middle key (tap)", "Right key"};
};

constexpr uint32_t kDefaultCodes[3] = {0x000C00B6, 0x000C00CD, 0x000C00B5};

// Ruler-striped canvas for judging scroll feel: hover it and turn the knob.
class TesterCanvas : public QWidget {
public:
    explicit TesterCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(6000);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const QPalette pal = palette();
        const int bandH = 25;
        for (int y = 0; y < height(); y += bandH) {
            const int band = y / bandH;
            if (band % 2 == 0) {
                p.fillRect(0, y, width(), bandH, pal.alternateBase());
            }
            if (band % 4 == 0) {
                p.setPen(pal.color(QPalette::PlaceholderText));
                p.drawLine(0, y, width(), y);
                p.setPen(pal.color(QPalette::Text));
                p.drawText(8, y + 17, QString::number(y));
            }
        }
    }
};

} // namespace

SettingsWindow::SettingsWindow(ScrollEngineSettings initial, KnobHidChannel *channel,
                               QWidget *parent)
    : QWidget(parent), current_(initial), channel_(channel) {
    setWindowTitle(tr("Knob"));
    setMinimumSize(460, 560);
    memcpy(keyCodes_, kDefaultCodes, sizeof(keyCodes_));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);

    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setObjectName("banner");
    layout->addWidget(banner_);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildScrollingTab(), tr("Scrolling"));
    tabs->addTab(buildKeysTab(), tr("Controls"));
    tabs->addTab(buildDeviceTab(), tr("Device"));
    layout->addWidget(tabs);

#ifdef Q_OS_MACOS
    if (channel_) {
        connect(channel_, &KnobHidChannel::presentChanged, this,
                &SettingsWindow::onChannelPresent);
        connect(channel_, &KnobHidChannel::keyLoaded, this, &SettingsWindow::onKeyLoaded);
        connect(channel_, &KnobHidChannel::committed, this, &SettingsWindow::onCommitted);
        connect(channel_, &KnobHidChannel::infoLoaded, this, &SettingsWindow::onInfoLoaded);
        connect(channel_, &KnobHidChannel::potConfigLoaded, this,
                &SettingsWindow::onPotConfigLoaded);
        connect(channel_, &KnobHidChannel::potValue, this, &SettingsWindow::onPotValue);
        potTimer_ = new QTimer(this);
        potTimer_->setInterval(400);
        connect(potTimer_, &QTimer::timeout, this, [this] {
            if (channel_ && channel_->channelPresent()) {
                channel_->requestPotValue();
            }
        });
        onChannelPresent(channel_->channelPresent());
    }
#endif

    checkLinearMouseConflict();
    refreshBanner();
    refreshReport();
}

QWidget *SettingsWindow::buildScrollingTab() {
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);

    // Speed in half-pixel steps: with 240 counts/rev the sweet spot is low.
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(1, 60); // 0.5 .. 30.0 px/count
    speedSlider_->setValue((int)qRound(current_.pxPerCount * 2.0));
    speedValue_ = new QLabel;
    speedValue_->setMinimumWidth(56);
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(speedSlider_, 1);
    speedRow->addWidget(speedValue_);
    form->addRow(tr("Speed"), speedRow);

    responseSlider_ = new QSlider(Qt::Horizontal);
    responseSlider_->setRange(20, 200);
    responseSlider_->setValue((int)current_.responseMs);
    responseValue_ = new QLabel;
    responseValue_->setMinimumWidth(56);
    auto *responseRow = new QHBoxLayout;
    responseRow->addWidget(responseSlider_, 1);
    responseRow->addWidget(responseValue_);
    form->addRow(tr("Response"), responseRow);

    invertBox_ = new QCheckBox(tr("Invert direction"));
    invertBox_->setChecked(current_.invert);
    form->addRow(QString(), invertBox_);

    phasesBox_ = new QCheckBox(tr("Report gesture phases (overscroll bounce)"));
    phasesBox_->setChecked(current_.reportPhases);
    form->addRow(QString(), phasesBox_);

    layout->addLayout(form);

    conflictLabel_ = new QLabel;
    conflictLabel_->setWordWrap(true);
    conflictLabel_->setObjectName("warning");
    conflictLabel_->hide();
    layout->addWidget(conflictLabel_);

    auto *testerBox = new QGroupBox(tr("Test area — hover here and turn the knob"));
    auto *testerLayout = new QVBoxLayout(testerBox);
    auto *scroller = new QScrollArea;
    scroller->setWidgetResizable(true);
    scroller->setWidget(new TesterCanvas);
    scroller->setMinimumHeight(180);
    testerLayout->addWidget(scroller);
    layout->addWidget(testerBox, 1);

    auto refresh = [this] {
        speedValue_->setText(tr("%1 px").arg(speedSlider_->value() / 2.0, 0, 'f', 1));
        responseValue_->setText(tr("%1 ms").arg(responseSlider_->value()));
    };
    refresh();

    connect(speedSlider_, &QSlider::valueChanged, this, [this, refresh] {
        refresh();
        emitChanged();
    });
    connect(responseSlider_, &QSlider::valueChanged, this, [this, refresh] {
        refresh();
        emitChanged();
    });
    connect(invertBox_, &QCheckBox::toggled, this, [this] { emitChanged(); });
    connect(phasesBox_, &QCheckBox::toggled, this, [this] { emitChanged(); });

    return tab;
}

QWidget *SettingsWindow::buildKeysTab() {
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    keysHint_ = new QLabel;
    keysHint_->setWordWrap(true);
    layout->addWidget(keysHint_);

    keysBox_ = new QGroupBox(tr("Key assignments"));
    auto *form = new QFormLayout(keysBox_);
    form->setHorizontalSpacing(14);

    for (int slot = 0; slot < 3; slot++) {
        keyCombo_[slot] = new QComboBox;
        QString lastCategory;
        for (const auto &kc : keycodeTable()) {
            if (lastCategory != kc.category) {
                if (!lastCategory.isEmpty()) {
                    keyCombo_[slot]->insertSeparator(keyCombo_[slot]->count());
                }
                lastCategory = kc.category;
            }
            keyCombo_[slot]->addItem(QString::fromUtf8(kc.label), QVariant::fromValue(kc.encoded));
        }
        form->addRow(tr(FirmwareDefaults::slotNames[slot]), keyCombo_[slot]);

        connect(keyCombo_[slot], &QComboBox::activated, this, [this, slot] {
            const uint32_t encoded = keyCombo_[slot]->currentData().toUInt();
            keyCodes_[slot] = encoded;
#ifdef Q_OS_MACOS
            if (channel_) {
                channel_->setKey((uint8_t)slot, encoded); // staged: try it now
                keysStatus_->setText(tr("Staged — try the key, then Save to keep it."));
            }
#endif
            refreshReport();
        });
    }

    auto *note = new QLabel(tr("Changes apply immediately for testing. “Save to knob” makes "
                               "them permanent (they persist across power-off). Modifiers "
                               "(⌘⇧⌃⌥) act while the key is held — great with the knob for "
                               "zoom or pan on the left/right keys. The middle key’s "
                               "hold-for-volume-layer is unaffected — you’re remapping its "
                               "tap, so modifiers aren’t useful there."));
    note->setWordWrap(true);
    note->setObjectName("secondary");
    form->addRow(QString(), note);

    layout->addWidget(keysBox_);

    // ---- Slide pot: role + per-role sensitivity + live value ----
    potBox_ = new QGroupBox(tr("Slide pot"));
    potForm_ = new QFormLayout(potBox_);
    auto *potForm = potForm_;
    potForm->setHorizontalSpacing(14);

    // Dual-pot hardware only (revealed by GET_INFO): the dedicated speed
    // slider's range, tuned here while the assignable pot does other work.
    speedRangeSlider_ = new QSlider(Qt::Horizontal);
    speedRangeSlider_->setRange(2, 8);
    speedRangeSlider_->setValue(potSpeedMax_);
    speedRangeValue_ = new QLabel;
    speedRangeValue_->setMinimumWidth(80);
    auto *speedRangeRow = new QHBoxLayout;
    speedRangeRow->addWidget(speedRangeSlider_, 1);
    speedRangeRow->addWidget(speedRangeValue_);
    speedRangeRow_ = potForm->rowCount();
    potForm->addRow(tr("Speed range"), speedRangeRow);

    potRoleCombo_ = new QComboBox;
    populatePotRoles();
    potForm->addRow(tr("Function"), potRoleCombo_);

    connect(speedRangeSlider_, &QSlider::valueChanged, this, [this](int v) {
        potSpeedMax_ = v;
        potSpeedMinDiv_ = v + 1;
        speedRangeValue_->setText(tr("÷%1 … ×%2").arg(v + 1).arg(v));
        sendPotConfig();
    });

    potSensSlider_ = new QSlider(Qt::Horizontal);
    potSensValue_ = new QLabel;
    potSensValue_->setMinimumWidth(80);
    auto *sensRow = new QHBoxLayout;
    sensRow->addWidget(potSensSlider_, 1);
    sensRow->addWidget(potSensValue_);
    potForm->addRow(tr("Sensitivity"), sensRow);

    potBar_ = new QProgressBar;
    potBar_->setRange(0, 3750); // SAADC full scale for the pot rail
    potBar_->setTextVisible(false);
    potBar_->setFixedHeight(8);
    potValueLabel_ = new QLabel(tr("—"));
    auto *valueRow = new QHBoxLayout;
    valueRow->addWidget(potBar_, 1);
    valueRow->addWidget(potValueLabel_);
    potForm->addRow(tr("Position"), valueRow);

    potForm->setRowVisible(speedRangeRow_, false); // shown when GET_INFO says dual-pot

    auto *potNote = new QLabel(tr("Each function keeps its own sensitivity — switching "
                                  "functions never resets the other's setting."));
    potNote->setWordWrap(true);
    potNote->setObjectName("secondary");
    potForm->addRow(QString(), potNote);

    layout->addWidget(potBox_);

    connect(potRoleCombo_, &QComboBox::activated, this, [this] {
        potRole_ = potRoleCombo_->currentData().toInt();
        updatePotSensUi();
        sendPotConfig();
    });
    connect(potSensSlider_, &QSlider::valueChanged, this, [this](int v) {
        if (potRole_ == 0) {
            potSpeedMax_ = v;
            potSpeedMinDiv_ = v + 1;
        } else {
            potSteps_ = v;
        }
        updatePotSensUi();
        sendPotConfig();
    });
    updatePotSensUi();

    auto *buttonRow = new QHBoxLayout;
    keysStatus_ = new QLabel;
    keysStatus_->setObjectName("secondary");
    buttonRow->addWidget(keysStatus_, 1);
    auto *resetButton = new QPushButton(tr("Reset defaults"));
    saveButton_ = new QPushButton(tr("Save to knob"));
    saveButton_->setDefault(true);
    buttonRow->addWidget(resetButton);
    buttonRow->addWidget(saveButton_);
    layout->addLayout(buttonRow);
    layout->addStretch(1);

#ifdef Q_OS_MACOS
    connect(saveButton_, &QPushButton::clicked, this, [this] {
        if (channel_) {
            channel_->commit();
        }
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        if (channel_) {
            channel_->resetDefaults();
            channel_->requestKeys();
        }
    });
#else
    Q_UNUSED(resetButton);
#endif

    onChannelPresent(false); // disabled until the channel reports in
    return tab;
}

QWidget *SettingsWindow::buildDeviceTab() {
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    connectionLabel_ = new QLabel;
    layout->addWidget(connectionLabel_);

    reportView_ = new QPlainTextEdit;
    reportView_->setReadOnly(true);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    reportView_->setFont(mono);
    layout->addWidget(reportView_, 1);

    auto *copyButton = new QPushButton(tr("Copy report"));
    connect(copyButton, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(reportView_->toPlainText()); });
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(copyButton);
    layout->addLayout(buttonRow);

    return tab;
}

void SettingsWindow::onChannelPresent(bool present) {
    if (!keysBox_) {
        return;
    }
    keysBox_->setEnabled(present);
    if (potBox_) {
        potBox_->setEnabled(present);
    }
    if (saveButton_) {
        saveButton_->setEnabled(present);
    }
    if (present) {
        keysHint_->setText(tr("Connected over USB — changes stage instantly."));
        keysLoaded_ = false;
#ifdef Q_OS_MACOS
        if (channel_) {
            channel_->requestKeys();
            channel_->requestPotConfig();
        }
        if (potTimer_) {
            potTimer_->start();
        }
#endif
    } else {
        keysHint_->setText(tr("🔌 Plug the knob in over USB to remap keys and the pot. (Scroll "
                              "smoothing works over Bluetooth; the settings channel is USB for "
                              "now.)"));
#ifdef Q_OS_MACOS
        if (potTimer_) {
            potTimer_->stop();
        }
#endif
        if (potValueLabel_) {
            potValueLabel_->setText(tr("—"));
        }
    }
    refreshBanner();
    refreshReport();
}

void SettingsWindow::populatePotRoles() {
    const QSignalBlocker block(potRoleCombo_);
    potRoleCombo_->clear();
    if (!hasFixedSpeed_) {
        potRoleCombo_->addItem(tr("Scroll speed (÷ … ×)"), 0);
    }
    potRoleCombo_->addItem(tr("Horizontal scroll"), 1);
    potRoleCombo_->addItem(tr("Volume"), 2);
    potRoleCombo_->addItem(tr("Off"), 3);
}

void SettingsWindow::onInfoLoaded(int protoVersion, int keySlots, int flags) {
    Q_UNUSED(protoVersion);
    Q_UNUSED(keySlots);
    const bool fixedSpeed = flags & 0x01;
    if (fixedSpeed == hasFixedSpeed_) {
        return;
    }
    hasFixedSpeed_ = fixedSpeed;
    populatePotRoles();
    if (potForm_ && speedRangeRow_ >= 0) {
        potForm_->setRowVisible(speedRangeRow_, hasFixedSpeed_);
    }
    if (hasFixedSpeed_ && potBox_) {
        potBox_->setTitle(tr("Sliders — speed (fixed) + assignable pot"));
    }
    updatePotSensUi();
    refreshReport();
}

void SettingsWindow::onPotConfigLoaded(int role, int speedMax, int speedMinDiv, int steps) {
    potRole_ = role;
    potSpeedMax_ = speedMax;
    potSpeedMinDiv_ = speedMinDiv;
    potSteps_ = steps;
    {
        const QSignalBlocker blockCombo(potRoleCombo_);
        // Dual-pot: a stored "speed" role is inert (the fixed slider owns
        // speed) — show it as Off rather than an unlisted entry.
        const int displayRole = (hasFixedSpeed_ && role == 0) ? 3 : role;
        const int idx = potRoleCombo_->findData(displayRole);
        if (idx >= 0) {
            potRoleCombo_->setCurrentIndex(idx);
        }
    }
    if (speedRangeSlider_) {
        const QSignalBlocker blockRange(speedRangeSlider_);
        speedRangeSlider_->setValue(qBound(2, potSpeedMax_, 8));
        speedRangeValue_->setText(tr("÷%1 … ×%2").arg(potSpeedMinDiv_).arg(potSpeedMax_));
    }
    updatePotSensUi();
    refreshReport();
}

void SettingsWindow::onPotValue(int raw, int role, int semantic) {
    potLastRaw_ = raw;
    potLastSemantic_ = semantic;
    if (potBar_) {
        potBar_->setValue(qBound(0, raw, 3750));
    }
    if (potValueLabel_) {
        QString text;
        switch (role) {
        case 0:
            text = semantic >= 1 ? tr("×%1").arg(semantic) : tr("÷%1").arg(-semantic);
            break;
        case 1:
        case 2:
            text = tr("step %1/%2").arg(semantic).arg(potSteps_);
            break;
        default:
            text = tr("%1%").arg(raw * 100 / 3750);
            break;
        }
        potValueLabel_->setText(text);
    }
}

void SettingsWindow::sendPotConfig() {
#ifdef Q_OS_MACOS
    if (channel_) {
        channel_->setPotConfig((uint8_t)potRole_, (uint8_t)potSpeedMax_,
                               (uint8_t)potSpeedMinDiv_, (uint8_t)potSteps_);
        keysStatus_->setText(tr("Staged — try it, then Save to keep it."));
    }
#endif
    refreshReport();
}

void SettingsWindow::updatePotSensUi() {
    if (!potSensSlider_) {
        return;
    }
    const QSignalBlocker block(potSensSlider_);
    if (hasFixedSpeed_ && potRole_ == 0) {
        // stored speed role is inert on dual-pot hardware: nothing to tune
        potSensValue_->setText(tr("—"));
        potSensSlider_->setEnabled(false);
    } else if (potRole_ == 0) {
        potSensSlider_->setRange(2, 8);
        potSensSlider_->setValue(qBound(2, potSpeedMax_, 8));
        potSensValue_->setText(tr("÷%1 … ×%2").arg(potSpeedMax_ + 1).arg(potSpeedMax_));
        potSensSlider_->setEnabled(true);
    } else if (potRole_ == 1 || potRole_ == 2) {
        potSensSlider_->setRange(4, 64);
        potSensSlider_->setValue(qBound(4, potSteps_, 64));
        potSensValue_->setText(tr("%1 steps").arg(potSteps_));
        potSensSlider_->setEnabled(true);
    } else {
        potSensValue_->setText(tr("—"));
        potSensSlider_->setEnabled(false);
    }
}

void SettingsWindow::onKeyLoaded(int slot, uint32_t encoded) {
    if (slot < 0 || slot >= 3) {
        return;
    }
    keyCodes_[slot] = encoded;
    keysLoaded_ = true;
    const int idx = keyCombo_[slot]->findData(QVariant::fromValue(encoded));
    if (idx >= 0) {
        keyCombo_[slot]->setCurrentIndex(idx);
    }
    keysStatus_->setText(tr("Loaded from knob."));
    refreshReport();
}

void SettingsWindow::onCommitted(bool ok) {
    keysStatus_->setText(ok ? tr("✓ Saved to knob — survives power-off.")
                            : tr("Save failed — check the USB connection."));
}

void SettingsWindow::setDeviceConnected(bool connected) {
    deviceConnected_ = connected;
    refreshBanner();
    refreshReport();
}

void SettingsWindow::setRawCountsActive(bool active) {
    rawCounts_ = active;
    refreshBanner();
    refreshReport();
}

void SettingsWindow::refreshBanner() {
    if (!banner_) {
        return;
    }
    if (deviceConnected_ && rawCounts_) {
        banner_->setText(tr("● Knob connected — smooth scrolling active (raw counts)"));
        banner_->setProperty("state", "good");
    } else if (!deviceConnected_) {
        banner_->setText(tr("○ Knob not connected — pair over Bluetooth or plug in USB"));
        banner_->setProperty("state", "warn");
    } else {
        banner_->setText(tr("△ Running in fallback mode — grant Input Monitoring in System "
                            "Settings → Privacy & Security for exact, acceleration-free "
                            "scrolling, then relaunch"));
        banner_->setProperty("state", "warn");
    }
    banner_->style()->unpolish(banner_);
    banner_->style()->polish(banner_);
}

void SettingsWindow::emitChanged() {
    current_.pxPerCount = speedSlider_->value() / 2.0;
    current_.responseMs = responseSlider_->value();
    current_.invert = invertBox_->isChecked();
    current_.reportPhases = phasesBox_->isChecked();
    if (onChanged) {
        onChanged(current_);
    }
    refreshReport();
}

QString SettingsWindow::buildReport() const {
    QString report;
    report += QStringLiteral("== Knob settings report ==\n");
    report += QStringLiteral("App version:      %1\n").arg(QCoreApplication::applicationVersion());
    report += QStringLiteral("Device:           %1 (VID 0x1d50, PID 0x615e)\n")
                  .arg(deviceConnected_ ? QStringLiteral("connected")
                                        : QStringLiteral("not connected"));
    report += QStringLiteral("\n-- Host (this app, live) --\n");
    report += QStringLiteral("Speed:            %1 px/count\n").arg(current_.pxPerCount);
    report += QStringLiteral("Response:         %1 ms\n").arg(current_.responseMs);
    report += QStringLiteral("Invert:           %1\n").arg(current_.invert ? "yes" : "no");
    report += QStringLiteral("Gesture phases:   %1\n").arg(current_.reportPhases ? "yes" : "no");
    report += QStringLiteral("Smooth scrolling: %1\n").arg(current_.enabled ? "on" : "off");
    report += QStringLiteral("Count source:     %1\n")
                  .arg(rawCounts_
                           ? QStringLiteral("raw HID (exact, acceleration-free)")
                           : QStringLiteral("intercepted events (OS curve!) — grant Input "
                                            "Monitoring"));
    report += QStringLiteral("\n-- Keys (%1) --\n")
                  .arg(keysLoaded_ ? QStringLiteral("live from knob")
                                   : QStringLiteral("defaults; connect USB for live values"));
    for (int i = 0; i < 3; i++) {
        report += QStringLiteral("%1: %2\n")
                      .arg(QString::fromUtf8(FirmwareDefaults::slotNames[i]), -17)
                      .arg(keycodeLabelFor(keyCodes_[i]));
    }
    static const char *potRoles[] = {"scroll speed", "horizontal scroll", "volume", "off"};
    report += QStringLiteral("\n-- Slide pot --\n");
    if (hasFixedSpeed_) {
        report += QStringLiteral("Speed slider:     fixed hardware (029/AIN5), range /%1 ... x%2\n")
                      .arg(potSpeedMinDiv_)
                      .arg(potSpeedMax_);
    }
    report += QStringLiteral("Function:         %1\n")
                  .arg(QString::fromUtf8(potRoles[qBound(0, potRole_, 3)]));
    if (potRole_ == 0) {
        report += QStringLiteral("Sensitivity:      /%1 ... x%2\n")
                      .arg(potSpeedMinDiv_)
                      .arg(potSpeedMax_);
    } else if (potRole_ == 1 || potRole_ == 2) {
        report += QStringLiteral("Sensitivity:      %1 steps\n").arg(potSteps_);
    }
    if (potLastRaw_ >= 0) {
        report += QStringLiteral("Live position:    %1/3750 (semantic %2)\n")
                      .arg(potLastRaw_)
                      .arg(potLastSemantic_);
    }
    report += QStringLiteral("\n-- Firmware (as flashed, compile-time) --\n");
    report += QStringLiteral("Haptic detents:   %1 /rev\n").arg(FirmwareDefaults::detentsPerRev);
    report += QStringLiteral("Wheel counts:     %1 /rev at x1\n").arg(FirmwareDefaults::linesPerRev);
    return report;
}

void SettingsWindow::refreshReport() {
    if (connectionLabel_) {
        connectionLabel_->setText(deviceConnected_ ? tr("● Knob connected")
                                                   : tr("○ Knob not connected"));
    }
    if (reportView_) {
        reportView_->setPlainText(buildReport());
    }
}

void SettingsWindow::checkLinearMouseConflict() {
    const QString path = QDir::homePath() + "/.config/linearmouse/linearmouse.json";
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray content = file.readAll();
    if (content.contains("KnoBLE") || content.contains("0x615e")) {
        conflictLabel_->setText(
            tr("LinearMouse also has a scheme for this knob (%1). Remove that block or quit "
               "LinearMouse while this app is running, or both will transform the scroll.")
                .arg(path));
        conflictLabel_->show();
    }
}
