// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "texture_export_job.h"

#include <QElapsedTimer>
#include <QThread>

namespace {

constexpr qint64 ProgressIntervalMs = 100;

} // namespace

TextureExportJob::TextureExportJob(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> packs,
                                   QString outputDir, ResourceExportOptions options,
                                   QObject* parent)
    : QObject(parent), fs_(std::move(fs)), packs_(std::move(packs)),
      outputDir_(std::move(outputDir)), options_(options) {
    qRegisterMetaType<TextureBatchStats>();
}

TextureExportJob::~TextureExportJob() {
    if (thread_) {
        cancel_ = true;
        thread_->wait();
        delete thread_;
    }
}

void TextureExportJob::start() {
    if (thread_) {
        return;
    }
    thread_ = QThread::create([this] { run(); });
    thread_->start();
}

void TextureExportJob::cancel() {
    cancel_ = true;
}

void TextureExportJob::run() {
    TextureBatch batch(fs_, outputDir_, options_);
    QStringList errors;
    const int total = int(packs_.size());
    QElapsedTimer sinceProgress;
    sinceProgress.start();
    emit progress(0, total, 0, QString());
    int done = 0;
    for (int entry : packs_) {
        if (cancel_) {
            break;
        }
        if (sinceProgress.elapsed() >= ProgressIntervalMs) {
            sinceProgress.restart();
            emit progress(done, total, batch.stats().textures,
                          fs_->index().entries[size_t(entry)].name);
        }
        batch.exportPack(entry, &errors);
        ++done;
    }
    emit progress(done, total, batch.stats().textures, QString());
    emit finished(batch.stats(), errors, cancel_.load());
}
