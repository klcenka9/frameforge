// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/project.h"
#include <QObject>
#include <QProcess>
namespace ff {
QStringList renderArguments(const Project &, const QString &temporaryOutput, const QString &encoder);
class RenderJob : public QObject {
    Q_OBJECT
  public:
    explicit RenderJob(QObject *parent = nullptr);
    ~RenderJob() override;
    void start(const Project &, const QString &output, const QString &encoder = "mpeg4");
    void cancel();
    bool busy() const {
        return active;
    }
  signals:
    void progress(int percent);
    void finished(bool success, const QString &message);

  private:
    QProcess process;
    QString target, temp, errors;
    QByteArray pending;
    qint64 durationUs = 0;
    bool active = false, cancelled = false, validating = false;
    void complete(bool success, const QString &message);
};
} // namespace ff
