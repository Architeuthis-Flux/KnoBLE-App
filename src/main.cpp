#include "app/TrayApp.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false); // menu-bar app: closing Settings ≠ quit
    app.setApplicationName("Knob");
    app.setOrganizationName("BaselineDesign");

    TrayApp tray;

    return app.exec();
}
