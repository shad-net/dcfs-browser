// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "extract_job.h"

#include "evo_reader.h"

#include <QElapsedTimer>
#include <QThread>

namespace {

constexpr qint64 ProgressIntervalMs = 100;

} // namespace

ExtractJob::ExtractJob(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> entries,
                       QString outputDir, bool verify, QObject* parent)
    : QObject(parent), fs_(std::move(fs)), entries_(std::move(entries)),
      outputDir_(std::move(outputDir)), verify_(verify) {}

ExtractJob::~ExtractJob() {
    if (thread_) {
        cancel_ = true;
        thread_->wait();
        delete thread_;
    }
}

void ExtractJob::start() {
    if (thread_) {
        return;
    }
    thread_ = QThread::create([this] { run(); });
    thread_->start();
}

void ExtractJob::cancel() {
    cancel_ = true;
}

void ExtractJob::run() {
    EvoReader reader(fs_);
    const auto& all = fs_->index().entries;
    const int total = int(entries_.size());
    qint64 totalBytes = 0;
    for (int i : entries_) {
        totalBytes += std::max(0, all[size_t(i)].size);
    }

    QStringList errors;
    int extracted = 0;
    qint64 bytesBefore = 0;
    QElapsedTimer sinceProgress;
    sinceProgress.start();
    emit progress(0, total, 0, totalBytes, QString());

    for (int n = 0; n < total && !cancel_; ++n) {
        const EvoIndexEntry& entry = all[size_t(entries_[size_t(n)])];
        auto onBytes = [&](qint64 bytes) {
            if (sinceProgress.elapsed() >= ProgressIntervalMs) {
                sinceProgress.restart();
                emit progress(n, total, bytesBefore + bytes, totalBytes, entry.name);
            }
        };
        QString error;
        if (reader.extractTo(entry, outputDir_, verify_, &error, &cancel_, onBytes)) {
            ++extracted;
        } else if (!cancel_) {
            errors << QStringLiteral("%1: %2").arg(entry.name, error);
        }
        bytesBefore += std::max(0, entry.size);
        if (sinceProgress.elapsed() >= ProgressIntervalMs) {
            sinceProgress.restart();
            emit progress(n + 1, total, bytesBefore, totalBytes, entry.name);
        }
    }
    emit progress(extracted + int(errors.size()), total, bytesBefore, totalBytes, QString());
    emit finished(extracted, errors, cancel_.load());
}
