// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QWaitCondition>

#include <memory>
#include <vector>

class QThread;

class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    ThumbnailLoader(std::shared_ptr<const EvoFileSystem> fs, int maxSize,
                    QObject* parent = nullptr);
    ~ThumbnailLoader() override;
    void request(int entry);

signals:
    void loaded(int entry, const QImage& thumbnail, const QString& info, bool hasTexture);

private:
    void run();

    std::shared_ptr<const EvoFileSystem> fs_;
    int maxSize_;
    QThread* thread_;
    QMutex mutex_;
    QWaitCondition wake_;
    std::vector<int> stack_;
    QSet<int> waiting_;
    bool quit_ = false;
};
