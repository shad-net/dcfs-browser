// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

class QThread;

// One archive found by a scan, already opened, so showing it needs no second parse.
struct ScannedArchive {
    struct DataFile {
        int index = 0;
        QString name;
        int files = 0;
        quint64 size = 0; // unpacked bytes of the files it holds
        bool present = false;
        QString error;
    };

    QString folder; // absolute
    std::shared_ptr<const EvoFileSystem> fs;
    quint64 size = 0;
    int missing = 0; // data files that are missing or unreadable
    std::vector<DataFile> dataFiles;
};
Q_DECLARE_METATYPE(std::shared_ptr<ScannedArchive>)

class ArchiveScanner : public QObject {
    Q_OBJECT
public:
    explicit ArchiveScanner(QString root, QObject* parent = nullptr);
    ~ArchiveScanner() override;

    void start();
    void cancel();
    const QString& root() const {
        return root_;
    }

signals:
    void progress(int folders, const QString& current);
    void found(const std::shared_ptr<ScannedArchive>& archive);
    void finished(int archives, int orphans, const QStringList& failed, bool cancelled);

private:
    void run();

    QString root_;
    std::atomic_bool cancel_{false};
    QThread* thread_ = nullptr;
};
