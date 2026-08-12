#include "app/TrayApp.h"

#include <QApplication>
#include <QPalette>

// Light-touch styling on top of the native macOS look: status banner chip,
// secondary/warning text roles, a bit more air. Colors derive from the
// system palette so light/dark both work.
static QString buildStyleSheet() {
    const QPalette pal = QApplication::palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const QString goodBg = dark ? "#1d3323" : "#e4f3e6";
    const QString goodFg = dark ? "#7fd18a" : "#1d7a2c";
    const QString warnBg = dark ? "#3a2f1a" : "#fbf1dd";
    const QString warnFg = dark ? "#e0b25c" : "#8a6116";
    const QString secondary = dark ? "#9a9a9e" : "#6e6e73";

    return QString(R"(
        QLabel#banner {
            padding: 8px 12px;
            border-radius: 8px;
            font-weight: 600;
        }
        QLabel#banner[state="good"] { background: %1; color: %2; }
        QLabel#banner[state="warn"] { background: %3; color: %4; }
        QLabel#secondary { color: %5; font-size: 12px; }
        QLabel#warning { color: %4; }
        QGroupBox {
            font-weight: 600;
            border: 1px solid palette(mid);
            border-radius: 8px;
            margin-top: 10px;
            padding-top: 6px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QTabWidget::pane { border: 0; }
    )")
        .arg(goodBg, goodFg, warnBg, warnFg, secondary);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false); // menu-bar app: closing Settings ≠ quit
    app.setApplicationName("Knob");
    app.setApplicationVersion("0.2.0");
    app.setOrganizationName("BaselineDesign");
    app.setStyleSheet(buildStyleSheet());

    TrayApp tray;

    return app.exec();
}
