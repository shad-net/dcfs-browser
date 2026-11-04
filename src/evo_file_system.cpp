// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "evo_file_system.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

const QString IndexName = QStringLiteral("game.ndx");
const QString AlphaDataName = QStringLiteral("game.dat");

QString tr(const char* text) {
    return QCoreApplication::translate("EvoFileSystem", text);
}

// "game012.dat" -> 12, anything else -> -1.
int dataIndexFromName(const QString& fileName) {
    static const QRegularExpression pattern(QStringLiteral("^game(\\d{3,5})\\.dat$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

} // namespace

bool EvoFileSystem::open(const QString& path, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    const QFileInfo info(path);
    if (!info.exists()) {
        return fail(tr("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
    }

    QString indexFile;
    QString alphaFile;
    if (info.isDir()) {
        const QDir dir(info.absoluteFilePath());
        if (QFileInfo::exists(dir.filePath(IndexName))) {
            indexFile = dir.filePath(IndexName);
        } else if (QFileInfo::exists(dir.filePath(AlphaDataName))) {
            alphaFile = dir.filePath(AlphaDataName);
        } else {
            return fail(tr("No game.ndx or game.dat in %1.")
                            .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
        }
    } else if (info.suffix().compare(QStringLiteral("ndx"), Qt::CaseInsensitive) == 0) {
        indexFile = info.absoluteFilePath();
    } else {
        // A .dat: the alpha keeps its index inside, retail data files need the game.ndx.
        QString dataError;
        const auto header = readEvoDataFile(info.absoluteFilePath(), 0, &dataError);
        if (!header) {
            return fail(dataError);
        }
        if (header->embedded) {
            alphaFile = info.absoluteFilePath();
        } else {
            indexFile = info.dir().filePath(IndexName);
            if (!QFileInfo::exists(indexFile)) {
                return fail(tr("%1 is a Driveclub data file, but its file names are stored in "
                               "game.ndx, which is not in the same folder.")
                                .arg(info.fileName()));
            }
            focusDataIndex_ = dataIndexFromName(info.fileName());
        }
    }

    if (!alphaFile.isEmpty()) {
        QString dataError;
        auto data = readEvoDataFile(alphaFile, 0, &dataError);
        if (!data) {
            return fail(dataError);
        }
        if (!data->embedded) {
            return fail(tr("%1 has no embedded index.").arg(QDir::toNativeSeparators(alphaFile)));
        }
        index_ = std::move(*data->embedded);
        data->embedded.reset();
        directory_ = QFileInfo(alphaFile).absolutePath();
        indexPath_ = QFileInfo(alphaFile).absoluteFilePath();
        dataFiles_.emplace(0, std::move(*data));
    } else {
        QFile file(indexFile);
        if (!file.open(QIODevice::ReadOnly)) {
            return fail(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(indexFile),
                                                     file.errorString()));
        }
        QString indexError;
        auto index = readEvoIndex(file, &indexError);
        if (!index) {
            return fail(
                QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(indexFile), indexError));
        }
        index_ = std::move(*index);
        directory_ = QFileInfo(indexFile).absolutePath();
        indexPath_ = QFileInfo(indexFile).absoluteFilePath();
    }

    for (const EvoIndexEntry& entry : index_.entries) {
        ++fileCounts_[entry.datIndex];
    }
    loadDataFiles();
    return true;
}

void EvoFileSystem::loadDataFiles() {
    if (isAlpha()) {
        return;
    }
    for (int i : dataIndices()) {
        const QString path = QDir(directory_).filePath(dataFileName(i));
        if (!QFileInfo::exists(path)) {
            dataErrors_[i] = tr("missing");
            continue;
        }
        QString error;
        auto data = readEvoDataFile(path, i, &error);
        if (!data) {
            dataErrors_[i] = error;
            continue;
        }
        dataFiles_.emplace(i, std::move(*data));
    }
}

std::vector<int> EvoFileSystem::dataIndices() const {
    std::vector<int> out;
    for (int i = 0; i < index_.dataFileCount; ++i) {
        out.push_back(i);
    }
    for (const auto& [i, count] : fileCounts_) {
        if (i >= index_.dataFileCount) {
            out.push_back(i);
        }
    }
    if (out.empty() && isAlpha()) {
        out.push_back(0);
    }
    return out;
}

QString EvoFileSystem::dataFileName(int dataIndex) const {
    if (isAlpha()) {
        return QFileInfo(indexPath_).fileName();
    }
    return QStringLiteral("game%1.dat").arg(dataIndex, 3, 10, QLatin1Char('0'));
}

const EvoDataFile* EvoFileSystem::dataFile(int dataIndex) const {
    const auto it = dataFiles_.find(dataIndex);
    return it == dataFiles_.end() ? nullptr : &it->second;
}

QString EvoFileSystem::dataFileError(int dataIndex) const {
    const auto it = dataErrors_.find(dataIndex);
    return it == dataErrors_.end() ? QString() : it->second;
}

int EvoFileSystem::fileCount(int dataIndex) const {
    const auto it = fileCounts_.find(dataIndex);
    return it == fileCounts_.end() ? 0 : it->second;
}
