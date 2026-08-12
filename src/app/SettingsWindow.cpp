#include "app/SettingsWindow.h"

#include <QCheckBox>
#include <QClipboard>
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
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// Firmware values as flashed in the current KnoBLE build (compile-time
// devicetree). Shown read-only until the raw-HID settings channel (v0.2)
// makes them live. Keep in sync with KnoBLE boards/shields/knoble.
struct FirmwareDefaults {
    static constexpr int detentsPerRev = 16;
    static constexpr int linesPerRev = 240;
    static constexpr int wheelScaleMax = 4;   // pot top: ×4
    static constexpr int wheelScaleMinDiv = 5; // pot bottom: ÷5
    static constexpr int sliderCurvePower = 1; // linear pot travel
    static constexpr const char *keys = "prev-track / play-pause (hold: layer 1) / next-track";
    static constexpr const char *layer1 = "knob: volume (48 detents), left key: USB↔BLE";
};

// Ruler-striped canvas for judging scroll feel: hover it and turn the knob.
// Bands + numbered ticks make hops, stalls, and drift easy to see.
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

SettingsWindow::SettingsWindow(ScrollEngineSettings initial, QWidget *parent)
    : QWidget(parent), current_(initial) {
    setWindowTitle(tr("Knob Settings"));
    setMinimumSize(420, 520);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    tabs->addTab(buildScrollingTab(), tr("Scrolling"));
    tabs->addTab(buildDeviceTab(), tr("Device"));
    layout->addWidget(tabs);

    checkLinearMouseConflict();
    refreshReport();
}

QWidget *SettingsWindow::buildScrollingTab() {
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);
    auto *form = new QFormLayout;

    // Speed: pixels of scroll per wheel count, in 0.5 px steps (slider ticks
    // are half-pixels). With 240 counts/rev the sweet spot is small — a few
    // px per count — so fine steps down low matter more than a big ceiling.
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(1, 60); // 0.5 .. 30.0 px/count
    speedSlider_->setValue((int)qRound(current_.pxPerCount * 2.0));
    speedValue_ = new QLabel;
    auto *speedRow = new QHBoxLayout;
    speedRow->addWidget(speedSlider_, 1);
    speedRow->addWidget(speedValue_);
    form->addRow(tr("Speed"), speedRow);

    // Response: catch-up time constant. Lower = page glued tighter to the
    // knob; higher = softer glide.
    responseSlider_ = new QSlider(Qt::Horizontal);
    responseSlider_->setRange(20, 200);
    responseSlider_->setValue((int)current_.responseMs);
    responseValue_ = new QLabel;
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
    conflictLabel_->setStyleSheet("color: #b45309;");
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

QWidget *SettingsWindow::buildDeviceTab() {
    auto *tab = new QWidget;
    auto *layout = new QVBoxLayout(tab);

    connectionLabel_ = new QLabel;
    layout->addWidget(connectionLabel_);

    auto *firmwareBox = new QGroupBox(tr("On-device settings (keys, slide pot, detents)"));
    auto *fwLayout = new QVBoxLayout(firmwareBox);
    auto *fwNote = new QLabel(
        tr("These live in the knob's firmware. Runtime editing from this panel arrives with "
           "the settings channel (v0.2) — until then they're compiled in, shown below as "
           "flashed."));
    fwNote->setWordWrap(true);
    fwLayout->addWidget(fwNote);
    layout->addWidget(firmwareBox);

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

void SettingsWindow::setDeviceConnected(bool connected) {
    deviceConnected_ = connected;
    refreshReport();
}

void SettingsWindow::setRawCountsActive(bool active) {
    rawCounts_ = active;
    refreshReport();
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
    report += QStringLiteral("\n-- Firmware (as flashed, compile-time) --\n");
    report += QStringLiteral("Haptic detents:   %1 /rev\n").arg(FirmwareDefaults::detentsPerRev);
    report += QStringLiteral("Wheel counts:     %1 /rev at x1\n").arg(FirmwareDefaults::linesPerRev);
    report += QStringLiteral("Slide pot:        scroll speed, /%1 ... x%2, %3 travel\n")
                  .arg(FirmwareDefaults::wheelScaleMinDiv)
                  .arg(FirmwareDefaults::wheelScaleMax)
                  .arg(FirmwareDefaults::sliderCurvePower == 1 ? QStringLiteral("linear")
                                                               : QStringLiteral("curved"));
    report += QStringLiteral("Keys:             %1\n").arg(FirmwareDefaults::keys);
    report += QStringLiteral("Layer 1:          %1\n").arg(FirmwareDefaults::layer1);
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

// Both this app and LinearMouse transforming the knob at once feels wrong in
// a way that's very hard to diagnose from the outside, so detect the overlap.
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
