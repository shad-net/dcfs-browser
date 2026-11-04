// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "texture_previewer.h"

#include "resource_export.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>

TexturePreviewer::TexturePreviewer(QObject* parent) : QObject(parent) {
    thread_ = QThread::create([this] { run(); });
    thread_->start();
}

TexturePreviewer::~TexturePreviewer() {
    {
        QMutexLocker lock(&mutex_);
        quit_ = true;
        pending_.reset();
        wake_.wakeAll();
    }
    thread_->wait();
    delete thread_;
}

int TexturePreviewer::request(std::shared_ptr<const EvoFileSystem> fs, int entryIndex) {
    QMutexLocker lock(&mutex_);
    const int ticket = ++nextTicket_;
    latest_ = ticket;
    pending_ = Request{ticket, std::move(fs), entryIndex};
    wake_.wakeAll();
    return ticket;
}

void TexturePreviewer::cancel() {
    QMutexLocker lock(&mutex_);
    pending_.reset();
    latest_ = -1;
}

void TexturePreviewer::run() {
    while (true) {
        Request request;
        {
            QMutexLocker lock(&mutex_);
            while (!quit_ && !pending_) {
                wake_.wait(&mutex_);
            }
            if (quit_) {
                return;
            }
            request = std::move(*pending_);
            pending_.reset();
        }

        QString error;
        auto context = openPackFromArchive(request.fs, request.entryIndex, &error);
        if (!context) {
            if (latest_ == request.ticket) {
                emit ready(request.ticket, QImage(), QString(), error);
            }
            continue;
        }
        const auto& resources = context->pack().resources();
        const ResourceInfo* first = nullptr;
        int count = 0;
        for (const ResourceInfo& info : resources) {
            if (info.type() == quint16(ResourceType::PixelBuffer)) {
                first = first ? first : &info;
                ++count;
            }
        }
        if (!first || latest_ != request.ticket) {
            if (latest_ == request.ticket) {
                emit ready(request.ticket, QImage(), QString(), QString());
            }
            continue;
        }

        QImage image;
        QString info;
        if (const auto pb = context->pack().readPixelBuffer(*first, &error)) {
            info = tr("%1: %2 x %3, %4")
                       .arg(first->displayName())
                       .arg(pb->width)
                       .arg(pb->height)
                       .arg(evoPixelFormatName(pb->pixelFormat));
            if (count > 1) {
                info += tr(" (texture 1 of %1; open the pack for the rest)").arg(count);
            }
            if (pb->startMip != 0) {
                const QStringList top = context->topMipsPacks();
                if (!top.isEmpty()) {
                    info += tr(", full size from %1").arg(QFileInfo(top.first()).fileName());
                }
            }
            if (const auto texture = context->texture(*first, &error)) {
                image = textureImage(*texture);
            }
        }
        if (latest_ == request.ticket) {
            emit ready(request.ticket, image, info, image.isNull() ? error : QString());
        }
    }
}
