// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"
#include "resource_export.h"
#include "texture_batch.h"

#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>
#include <vector>

class QThread;

class TextureExportJob : public QObject {
    Q_OBJECT
public:
    TextureExportJob(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> packs,
                     QString outputDir, ResourceExportOptions options, QObject* parent = nullptr);
    ~TextureExportJob() override;

    void start();
    void cancel();
    const QString& outputDir() const {
        return outputDir_;
    }

signals:
    void progress(int packsDone, int packsTotal, int textures, const QString& current);
    void finished(const TextureBatchStats& stats, const QStringList& errors, bool cancelled);

private:
    void run();

    std::shared_ptr<const EvoFileSystem> fs_;
    std::vector<int> packs_;
    QString outputDir_;
    ResourceExportOptions options_;
    std::atomic_bool cancel_{false};
    QThread* thread_ = nullptr;
};
