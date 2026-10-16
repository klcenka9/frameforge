// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include <QImage>
namespace ff {
Media probe(const QString &path);
QImage thumbnail(const QString &path);
QStringList encoders();
QString diagnostics();
} // namespace ff
