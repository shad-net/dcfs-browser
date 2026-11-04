// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "evo_file_system.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class GalleryModel;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSlider;
class QSortFilterProxyModel;

class GalleryWindow : public QWidget {
    Q_OBJECT
public:
    struct Actions {
        std::function<void(int entry)> openPack;
        std::function<void(std::vector<int> entries, const QString& what)> exportTextures;
    };

    GalleryWindow(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> packs,
                  const QString& title, Actions actions, QWidget* parent);

private:
    std::vector<int> selectedPacks() const;
    void showContextMenu(const QPoint& pos);
    void applyIconSize(int size);
    void updateCount();

    std::shared_ptr<const EvoFileSystem> fs_;
    Actions actions_;
    QString title_;
    GalleryModel* model_;
    QSortFilterProxyModel* proxy_;
    QListView* view_;
    QLineEdit* filter_;
    QSlider* size_;
    QLabel* count_;
    QPushButton* export_;
};
