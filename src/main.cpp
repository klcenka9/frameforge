// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/editor.h"
#include "ui/theme.h"
#include <QApplication>
#include <QStandardPaths>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Frameforge");
    app.setOrganizationName("Frameforge");
    app.setApplicationVersion("0.2.0");

    ff::applyTheme(app);
    ff::Editor editor;
    editor.show();
    return app.exec();
}
