#include "app/SettingsWindow.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

SettingsWindow::SettingsWindow(ScrollEngineSettings initial, QWidget *parent)
    : QWidget(parent), current_(initial) {
    setWindowTitle(tr("Knob Settings"));
    setMinimumWidth(380);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    // Speed: pixels of scroll per wheel count. 48 counts/rev at the knob's
    // x1, so 18 px/count ~= 860 px per revolution.
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(2, 80);
    speedSlider_->setValue((int)current_.pxPerCount);
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

    auto refresh = [this] {
        speedValue_->setText(tr("%1 px").arg(speedSlider_->value()));
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

    checkLinearMouseConflict();
}

void SettingsWindow::emitChanged() {
    current_.pxPerCount = speedSlider_->value();
    current_.responseMs = responseSlider_->value();
    current_.invert = invertBox_->isChecked();
    current_.reportPhases = phasesBox_->isChecked();
    if (onChanged) {
        onChanged(current_);
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
