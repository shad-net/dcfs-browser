// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

#include <atomic>
#include <memory>
#include <optional>

class QThread;

class TexturePreviewer : public QObject {
    Q_OBJECT
public:
    explicit TexturePreviewer(QObject* parent = nullptr);
    ~TexturePreviewer() override;
    int request(std::shared_ptr<const EvoFileSystem> fs, int entryIndex);
    void cancel();

signals:
    void ready(int ticket, const QImage& image, const QString& info, const QString& error);

private:
    struct Request {
        int ticket;
        std::shared_ptr<const EvoFileSystem> fs;
        int entryIndex;
    };
    void run();

    QThread* thread_;
    QMutex mutex_;
    QWaitCondition wake_;
    std::optional<Request> pending_;
    bool quit_ = false;
    int nextTicket_ = 0;
    std::atomic_int latest_{-1};
};
