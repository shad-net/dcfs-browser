// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "archive_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>
#include <map>

namespace {

constexpr qint64 ProgressIntervalMs = 100;

std::shared_ptr<ScannedArchive> describe(const QString& folder,
                                         std::shared_ptr<const EvoFileSystem> fs) {
    auto archive = std::make_shared<ScannedArchive>();
    archive->folder = folder;
    std::map<int, ScannedArchive::DataFile> files;
    for (int i : fs->dataIndices()) {
        ScannedArchive::DataFile& data = files[i];
        data.index = i;
        data.name = fs->dataFileName(i);
        data.present = fs->dataFile(i) != nullptr;
        data.error = fs->dataFileError(i);
    }
    for (const EvoIndexEntry& entry : fs->index().entries) {
        ScannedArchive::DataFile& data = files[entry.datIndex];
        ++data.files;
        data.size += quint64(std::max(0, entry.size));
        archive->size += quint64(std::max(0, entry.size));
    }
    for (auto& [i, data] : files) {
        if (!data.present && data.files > 0) {
            ++archive->missing;
        }
        archive->dataFiles.push_back(std::move(data));
    }
    archive->fs = std::move(fs);
    return archive;
}

} // namespace

ArchiveScanner::ArchiveScanner(QString root, QObject* parent)
    : QObject(parent), root_(std::move(root)) {
    qRegisterMetaType<std::shared_ptr<ScannedArchive>>();
}

ArchiveScanner::~ArchiveScanner() {
    if (thread_) {
        cancel_ = true;
        thread_->wait();
        delete thread_;
    }
}

void ArchiveScanner::start() {
    if (thread_) {
        return;
    }
    thread_ = QThread::create([this] { run(); });
    thread_->start();
}

void ArchiveScanner::cancel() {
    cancel_ = true;
}

void ArchiveScanner::run() {
    static const QRegularExpression dataName(QStringLiteral("^game\\d{3,5}\\.dat$"),
                                             QRegularExpression::CaseInsensitiveOption);
    int folders = 0;
    int archives = 0;
    int orphans = 0;
    QStringList failed;
    QElapsedTimer sinceProgress;
    sinceProgress.start();

    auto visit = [&](const QString& folder) {
        ++folders;
        if (sinceProgress.elapsed() >= ProgressIntervalMs) {
            sinceProgress.restart();
            emit progress(folders, folder);
        }
        const QDir dir(folder);
        const QStringList names = dir.entryList({QStringLiteral("*.ndx"), QStringLiteral("*.dat")},
                                                QDir::Files | QDir::Readable);
        auto has = [&names](const QString& name) {
            return std::any_of(names.begin(), names.end(), [&name](const QString& n) {
                return n.compare(name, Qt::CaseInsensitive) == 0;
            });
        };
        const bool index = has(QStringLiteral("game.ndx"));
        const bool alpha = has(QStringLiteral("game.dat"));
        if (!index && !alpha) {
            orphans += int(std::count_if(names.begin(), names.end(), [](const QString& n) {
                return dataName.match(n).hasMatch();
            }));
            return;
        }
        auto fs = std::make_shared<EvoFileSystem>();
        QString error;
        if (!fs->open(folder, &error)) {
            failed << QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(folder), error);
            return;
        }
        ++archives;
        emit found(describe(QFileInfo(folder).absoluteFilePath(), std::move(fs)));
    };

    visit(root_);
    QDirIterator it(root_, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && !cancel_) {
        visit(it.next());
    }
    emit progress(folders, QString());
    emit finished(archives, orphans, failed, cancel_.load());
}
