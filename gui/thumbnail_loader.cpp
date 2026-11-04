// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "thumbnail_loader.h"

#include "resource_export.h"

#include <QMutexLocker>
#include <QThread>

ThumbnailLoader::ThumbnailLoader(std::shared_ptr<const EvoFileSystem> fs, int maxSize,
                                 QObject* parent)
    : QObject(parent), fs_(std::move(fs)), maxSize_(maxSize) {
    thread_ = QThread::create([this] { run(); });
    thread_->start();
}

ThumbnailLoader::~ThumbnailLoader() {
    {
        QMutexLocker lock(&mutex_);
        quit_ = true;
        wake_.wakeAll();
    }
    thread_->wait();
    delete thread_;
}

void ThumbnailLoader::request(int entry) {
    QMutexLocker lock(&mutex_);
    if (waiting_.contains(entry)) {
        return;
    }
    waiting_.insert(entry);
    stack_.push_back(entry);
    wake_.wakeAll();
}

void ThumbnailLoader::run() {
    ArchivePackSource source(fs_);
    while (true) {
        int entry;
        {
            QMutexLocker lock(&mutex_);
            while (!quit_ && stack_.empty()) {
                wake_.wait(&mutex_);
            }
            if (quit_) {
                return;
            }
            entry = stack_.back();
            stack_.pop_back();
            waiting_.remove(entry);
        }

        QString error;
        auto context = source.open(entry, &error);
        if (!context) {
            emit loaded(entry, QImage(), error, true);
            continue;
        }
        const ResourceInfo* first = nullptr;
        int count = 0;
        for (const ResourceInfo& info : context->pack().resources()) {
            if (info.type() == quint16(ResourceType::PixelBuffer)) {
                first = first ? first : &info;
                ++count;
            }
        }
        if (!first) {
            emit loaded(entry, QImage(), tr("No textures in this pack."), false);
            continue;
        }
        const auto pb = context->pack().readPixelBuffer(*first, &error);
        const auto texture = pb ? context->texture(*first, &error) : std::nullopt;
        if (!texture) {
            emit loaded(entry, QImage(), error, true);
            continue;
        }
        QImage image = textureImage(*texture);
        if (image.width() > maxSize_ || image.height() > maxSize_) {
            image = image.scaled(maxSize_, maxSize_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QString info = tr("%1 x %2, %3")
                           .arg(pb->width)
                           .arg(pb->height)
                           .arg(evoPixelFormatName(pb->pixelFormat));
        if (count > 1) {
            info += tr(", 1 of %1 textures").arg(count);
        }
        emit loaded(entry, image, info, true);
    }
}
