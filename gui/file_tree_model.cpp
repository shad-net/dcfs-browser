// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "file_tree_model.h"

#include <QApplication>
#include <QBrush>
#include <QHash>
#include <QLocale>
#include <QPalette>
#include <QStyle>

#include <algorithm>

QString formatSize(quint64 bytes) {
    if (bytes < 1024) {
        return QObject::tr("%1 B").arg(bytes);
    }
    const double value = double(bytes);
    if (bytes < 1024ull * 1024) {
        return QObject::tr("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024ull * 1024 * 1024) {
        return QObject::tr("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QObject::tr("%1 GB").arg(value / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString hexDump(const QByteArray& data) {
    QString out;
    out.reserve(int(data.size() / 16 + 1) * 78);
    for (qsizetype line = 0; line < data.size(); line += 16) {
        out += QStringLiteral("%1  ").arg(line, 8, 16, QLatin1Char('0'));
        QString ascii;
        for (int i = 0; i < 16; ++i) {
            if (line + i < data.size()) {
                const uchar c = uchar(data.at(line + i));
                out += QStringLiteral("%1 ").arg(c, 2, 16, QLatin1Char('0'));
                ascii += (c >= 0x20 && c < 0x7F) ? QChar(c) : QLatin1Char('.');
            } else {
                out += QLatin1String("   ");
            }
            if (i == 7) {
                out += QLatin1Char(' ');
            }
        }
        out += QLatin1Char(' ') + ascii + QLatin1Char('\n');
    }
    return out;
}

QString fileCount(qint64 count) {
    return count == 1 ? QObject::tr("1 file")
                      : QObject::tr("%1 files").arg(QLocale().toString(count));
}

FileTreeModel::FileTreeModel(QObject* parent) : QAbstractItemModel(parent) {
    folderIcon_ = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    fileIcon_ = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
}

void FileTreeModel::setFileSystem(std::shared_ptr<const EvoFileSystem> fs, int dataIndex) {
    beginResetModel();
    fs_ = std::move(fs);
    dataFilter_ = dataIndex;
    rebuild();
    endResetModel();
}

void FileTreeModel::setDataFilter(int dataIndex) {
    if (dataIndex == dataFilter_) {
        return;
    }
    beginResetModel();
    dataFilter_ = dataIndex;
    rebuild();
    endResetModel();
}

void FileTreeModel::rebuild() {
    nodes_.clear();
    nodes_.push_back(Node{});
    if (!fs_) {
        return;
    }

    QHash<QString, int> folders; // full folder path -> node
    folders.insert(QString(), 0);
    const auto& entries = fs_->index().entries;

    auto folderFor = [&](const QString& path) {
        // Walk down from the deepest folder we already have.
        int cut = int(path.size());
        int known = 0;
        while (cut > 0) {
            const auto it = folders.constFind(path.left(cut));
            if (it != folders.constEnd()) {
                known = *it;
                break;
            }
            cut = int(path.lastIndexOf(QLatin1Char('/'), cut - 1));
            if (cut < 0) {
                cut = 0;
            }
        }
        int node = known;
        while (cut < path.size()) {
            const int start = cut == 0 ? 0 : cut + 1;
            int next = int(path.indexOf(QLatin1Char('/'), start));
            if (next < 0) {
                next = int(path.size());
            }
            Node folder;
            folder.name = path.mid(start, next - start);
            folder.path = path.left(next);
            folder.parent = node;
            const int id = int(nodes_.size());
            nodes_.push_back(std::move(folder));
            nodes_[size_t(node)].children.push_back(id);
            folders.insert(nodes_.back().path, id);
            node = id;
            cut = next;
        }
        return node;
    };

    for (int i = 0; i < int(entries.size()); ++i) {
        const EvoIndexEntry& entry = entries[size_t(i)];
        if (dataFilter_ >= 0 && entry.datIndex != dataFilter_) {
            continue;
        }
        const int slash = int(entry.name.lastIndexOf(QLatin1Char('/')));
        const int parent = slash > 0 ? folderFor(entry.name.left(slash)) : 0;
        Node file;
        file.name = entry.name.mid(slash + 1);
        file.path = entry.name;
        file.parent = parent;
        file.entry = i;
        file.files = 1;
        file.size = quint64(std::max(0, entry.size));
        const int id = int(nodes_.size());
        nodes_.push_back(std::move(file));
        nodes_[size_t(parent)].children.push_back(id);
    }

    // Folder totals: children always have larger ids than their parent, so walk backwards.
    for (int id = int(nodes_.size()) - 1; id > 0; --id) {
        const Node& node = nodes_[size_t(id)];
        Node& parent = nodes_[size_t(node.parent)];
        parent.files += node.files;
        parent.size += node.size;
    }

    for (Node& node : nodes_) {
        std::sort(node.children.begin(), node.children.end(), [this](int a, int b) {
            const Node& x = nodes_[size_t(a)];
            const Node& y = nodes_[size_t(b)];
            if ((x.entry < 0) != (y.entry < 0)) {
                return x.entry < 0;
            }
            return x.name.compare(y.name, Qt::CaseInsensitive) < 0;
        });
        for (size_t row = 0; row < node.children.size(); ++row) {
            nodes_[size_t(node.children[row])].row = int(row);
        }
    }
}

int FileTreeModel::nodeId(const QModelIndex& index) const {
    return index.isValid() ? int(index.internalId()) : 0;
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (column < 0 || column >= ColumnCount || nodes_.empty()) {
        return {};
    }
    const Node& node = nodes_[size_t(nodeId(parent))];
    if (row < 0 || row >= int(node.children.size())) {
        return {};
    }
    return createIndex(row, column, quintptr(node.children[size_t(row)]));
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }
    const int parent = nodes_[size_t(nodeId(child))].parent;
    if (parent <= 0) {
        return {};
    }
    return createIndex(nodes_[size_t(parent)].row, 0, quintptr(parent));
}

int FileTreeModel::rowCount(const QModelIndex& parent) const {
    if (nodes_.empty() || parent.column() > 0) {
        return 0;
    }
    return int(nodes_[size_t(nodeId(parent))].children.size());
}

int FileTreeModel::columnCount(const QModelIndex&) const {
    return ColumnCount;
}

QVariant FileTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !fs_) {
        return {};
    }
    const Node& node = nodes_[size_t(nodeId(index))];
    const bool folder = node.entry < 0;
    const EvoIndexEntry* entry = folder ? nullptr : &fs_->index().entries[size_t(node.entry)];
    const bool missing = entry && !fs_->dataFile(entry->datIndex);

    switch (role) {
    case EntryRole:
        return node.entry;
    case PathRole:
        return node.path;
    case Qt::DecorationRole:
        if (index.column() == NameColumn) {
            return folder ? folderIcon_ : fileIcon_;
        }
        return {};
    case Qt::ForegroundRole:
        if (missing) {
            return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        }
        return {};
    case Qt::ToolTipRole:
        if (missing) {
            return tr("%1 is %2: this file cannot be previewed or extracted.")
                .arg(fs_->dataFileName(entry->datIndex), fs_->dataFileError(entry->datIndex));
        }
        return node.path;
    case Qt::TextAlignmentRole:
        if (index.column() == SizeColumn || index.column() == OffsetColumn) {
            return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    case SortRole:
        switch (index.column()) {
        case NameColumn:
            return node.name.toLower();
        case SizeColumn:
            return node.size;
        case DataFileColumn:
            return folder ? node.files : int(entry->datIndex);
        case OffsetColumn:
            return folder ? quint64(0) : entry->offset;
        case HashColumn:
            return folder ? 0u : entry->nameHash;
        }
        return {};
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return node.name;
        case SizeColumn:
            return formatSize(node.size);
        case DataFileColumn:
            return folder ? fileCount(node.files) : fs_->dataFileName(entry->datIndex);
        case OffsetColumn:
            return folder
                       ? QString()
                       : QStringLiteral("0x%1").arg(QString::number(entry->offset, 16).toUpper());
        case HashColumn:
            return folder ? QString()
                          : QStringLiteral("%1")
                                .arg(entry->nameHash, 8, 16, QLatin1Char('0'))
                                .toUpper();
        }
        return {};
    }
    return {};
}

QVariant FileTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case NameColumn:
        return tr("Name");
    case SizeColumn:
        return tr("Size");
    case DataFileColumn:
        return tr("Data file");
    case OffsetColumn:
        return tr("Offset");
    case HashColumn:
        return tr("Name hash");
    }
    return {};
}

FileTreeProxy::FileTreeProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setSortRole(FileTreeModel::SortRole);
    setFilterRole(FileTreeModel::PathRole);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setRecursiveFilteringEnabled(true);
    setAutoAcceptChildRows(true);
}

bool FileTreeProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const bool leftFolder = left.data(FileTreeModel::EntryRole).toInt() < 0;
    const bool rightFolder = right.data(FileTreeModel::EntryRole).toInt() < 0;
    if (leftFolder != rightFolder) {
        // Keep folders on top whichever way the column is sorted.
        return sortOrder() == Qt::AscendingOrder ? leftFolder : rightFolder;
    }
    return QSortFilterProxyModel::lessThan(left, right);
}
