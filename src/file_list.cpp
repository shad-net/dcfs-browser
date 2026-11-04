// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "file_list.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("FileList", text);
}

QString hex(quint64 value, int width) {
    return QString::number(value, 16).toUpper().rightJustified(width, QLatin1Char('0'));
}

QString csvField(const QString& text) {
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) &&
        !text.contains(QLatin1Char('\n'))) {
        return text;
    }
    return QLatin1Char('"') + QString(text).replace(QLatin1String("\""), QLatin1String("\"\"")) +
           QLatin1Char('"');
}

} // namespace

std::optional<FileListFormat> fileListFormatFromName(const QString& name) {
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("names") || n == QLatin1String("txt")) {
        return FileListFormat::Names;
    }
    if (n == QLatin1String("detailed")) {
        return FileListFormat::Detailed;
    }
    if (n == QLatin1String("csv")) {
        return FileListFormat::Csv;
    }
    return std::nullopt;
}

QString fileListSuffix(FileListFormat format) {
    return format == FileListFormat::Csv ? QStringLiteral("csv") : QStringLiteral("txt");
}

std::vector<int> entriesInDataFile(const EvoFileSystem& fs, int dataIndex) {
    std::vector<int> out;
    const auto& entries = fs.index().entries;
    for (int i = 0; i < int(entries.size()); ++i) {
        if (dataIndex < 0 || entries[size_t(i)].datIndex == dataIndex) {
            out.push_back(i);
        }
    }
    return out;
}

bool writeFileList(const EvoFileSystem& fs, std::vector<int> entries, const QString& path,
                   FileListFormat format, QString* error) {
    const auto& all = fs.index().entries;
    std::sort(entries.begin(), entries.end(), [&all](int a, int b) {
        const int order = all[size_t(a)].name.compare(all[size_t(b)].name, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : all[size_t(a)].name < all[size_t(b)].name;
    });

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (format == FileListFormat::Csv) {
        out << "path,size,data_file,offset,name_hash,md5\n";
    }
    for (int i : entries) {
        const EvoIndexEntry& e = all[size_t(i)];
        switch (format) {
        case FileListFormat::Names:
            out << e.name << '\n';
            break;
        case FileListFormat::Detailed:
            out << e.name << " (" << e.size << " bytes, Dat Index: " << e.datIndex << ", Offset: 0x"
                << hex(e.offset, 8) << ")\n";
            break;
        case FileListFormat::Csv:
            out << csvField(e.name) << ',' << e.size << ',' << csvField(fs.dataFileName(e.datIndex))
                << ",0x" << hex(e.offset, 8) << ",0x" << hex(e.nameHash, 8) << ','
                << QString::fromLatin1(e.md5.toHex()) << '\n';
            break;
        }
    }
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit()) {
        *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    return true;
}

int writeFileListPerDataFile(const EvoFileSystem& fs, const QString& folder, FileListFormat format,
                             QString* error) {
    if (!QDir().mkpath(folder)) {
        *error = tr("cannot create folder %1.").arg(QDir::toNativeSeparators(folder));
        return -1;
    }
    int written = 0;
    for (int i : fs.dataIndices()) {
        std::vector<int> entries = entriesInDataFile(fs, i);
        if (entries.empty()) {
            continue;
        }
        const QString name = QFileInfo(fs.dataFileName(i)).completeBaseName() + QLatin1Char('.') +
                             fileListSuffix(format);
        if (!writeFileList(fs, std::move(entries), QDir(folder).filePath(name), format, error)) {
            return -1;
        }
        ++written;
    }
    return written;
}
