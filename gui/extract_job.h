// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

class QThread;

class ExtractJob : public QObject {
    Q_OBJECT
public:
    ExtractJob(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> entries, QString outputDir,
               bool verify, QObject* parent = nullptr);
    ~ExtractJob() override;

    void start();
    void cancel();
    const QString& outputDir() const {
        return outputDir_;
    }

signals:
    void progress(int filesDone, int filesTotal, qint64 bytesDone, qint64 bytesTotal,
                  const QString& current);
    void finished(int extracted, const QStringList& errors, bool cancelled);

private:
    void run();

    std::shared_ptr<const EvoFileSystem> fs_;
    std::vector<int> entries_;
    QString outputDir_;
    bool verify_;
    std::atomic_bool cancel_{false};
    QThread* thread_ = nullptr;
};
