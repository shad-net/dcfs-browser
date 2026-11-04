// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QAbstractItemModel>
#include <QIcon>
#include <QSortFilterProxyModel>

#include <memory>
#include <vector>

class FileTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { NameColumn, SizeColumn, DataFileColumn, OffsetColumn, HashColumn, ColumnCount };
    enum Role {
        EntryRole = Qt::UserRole + 1, // index into EvoIndex::entries, -1 for folders
        SortRole,
        PathRole, // full path, used for filtering
    };

    explicit FileTreeModel(QObject* parent = nullptr);

    // dataIndex -1 shows every file.
    void setFileSystem(std::shared_ptr<const EvoFileSystem> fs, int dataIndex = -1);
    void setDataFilter(int dataIndex);
    int dataFilter() const {
        return dataFilter_;
    }
    int visibleFileCount() const {
        return nodes_.empty() ? 0 : nodes_.front().files;
    }
    quint64 visibleSize() const {
        return nodes_.empty() ? 0 : nodes_.front().size;
    }

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    struct Node {
        QString name;
        QString path;
        int parent = -1;
        int row = 0;
        int entry = -1;
        int files = 0;
        quint64 size = 0;
        std::vector<int> children;
    };

    void rebuild();
    int nodeId(const QModelIndex& index) const;

    std::shared_ptr<const EvoFileSystem> fs_;
    int dataFilter_ = -1;
    std::vector<Node> nodes_; // nodes_[0] is the root
    QIcon folderIcon_;
    QIcon fileIcon_;
};

class FileTreeProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FileTreeProxy(QObject* parent = nullptr);

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

QString formatSize(quint64 bytes);
QString fileCount(qint64 count);
QString hexDump(const QByteArray& data);
