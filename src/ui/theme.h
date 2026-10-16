// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
namespace ff {
inline void applyTheme(QApplication &app) {
    app.setStyle("Fusion");
    app.setStyleSheet("QWidget { background: #25272c; color: #d6d9df; font-size: 12px; } QMenuBar,QToolBar "
                      "{background:#1d1f23;} QDockWidget::title { background:#30333a; padding:6px; } "
                      "QPushButton,QComboBox,QSpinBox { background:#383c44; border:1px solid #4b505a; "
                      "border-radius:3px; padding:5px;} QPushButton:hover {background:#4a515c;} "
                      "QWidget:focus { border-color:#e7b366; } QLineEdit,QListWidget {background:#1b1d22;} "
                      "QSlider::groove:horizontal {height:4px;background:#4a4f59;} "
                      "QSlider::handle:horizontal {width:12px;background:#e7b366;margin:-5px 0;} "
                      "QToolButton:checked {background:#6d5736;} QToolButton:disabled {color:#737780;} "
                      "QProgressBar::chunk {background:#ba914e;} QToolTip {background:#42464e;color:white;}");
}
} // namespace ff
