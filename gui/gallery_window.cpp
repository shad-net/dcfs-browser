// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gallery_window.h"

#include "thumbnail_loader.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QCache>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int ThumbnailSize = 160;         // stored size; the view scales down from it
constexpr int CacheKilobytes = 160 * 1024; // about 1,600 thumbnails at full size
constexpr int EntryRole = Qt::UserRole + 1;
constexpr int PathRole = Qt::UserRole + 2;

// "a/b/x.tif_pixel_buffer.rpk" -> "x"
QString displayName(const QString& path) {
    QString name = QFileInfo(path).completeBaseName();
    if (name.endsWith(QLatin1String("_pixel_buffer"), Qt::CaseInsensitive)) {
        name.chop(int(qstrlen("_pixel_buffer")));
    }
    const QFileInfo inner(name);
    static const QStringList imageSuffixes = {QStringLiteral("tif"), QStringLiteral("tiff"),
                                              QStringLiteral("tga"), QStringLiteral("png"),
                                              QStringLiteral("dds"), QStringLiteral("psd")};
    if (imageSuffixes.contains(inner.suffix().toLower())) {
        name = inner.completeBaseName();
    }
    return name;
}

QPixmap placeholder(int size, const QColor& color) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(4, 4, size - 8, size - 8), 6, 6);
    return pixmap;
}

} // namespace

class GalleryModel : public QAbstractListModel {
public:
    GalleryModel(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> packs, QObject* parent)
        : QAbstractListModel(parent), fs_(std::move(fs)), packs_(std::move(packs)) {
        cache_.setMaxCost(CacheKilobytes);
        for (int row = 0; row < int(packs_.size()); ++row) {
            const QString& path = fs_->index().entries[size_t(packs_[size_t(row)])].name;
            names_ << displayName(path);
            rowOf_.insert(packs_[size_t(row)], row);
        }
        loader_ = new ThumbnailLoader(fs_, ThumbnailSize, this);
        connect(loader_, &ThumbnailLoader::loaded, this, &GalleryModel::loaded);
        loading_ = QIcon(placeholder(ThumbnailSize, QColor(128, 128, 128, 60)));
        const QStyle* style = QApplication::style();
        failedIcon_ = style->standardIcon(QStyle::SP_MessageBoxWarning);
        noTexture_ = style->standardIcon(QStyle::SP_FileIcon);
    }

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : int(packs_.size());
    }

    int loadedCount() const {
        return loadedCount_;
    }

    // Every cell is this size: room for the thumbnail and two lines of name.
    void setItemSize(const QSize& size) {
        beginResetModel();
        itemSize_ = size;
        endResetModel();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= int(packs_.size())) {
            return {};
        }
        const int entry = packs_[size_t(index.row())];
        switch (role) {
        case Qt::DisplayRole: {
            // Let long names wrap after underscores (zero-width spaces are invisible).
            QString name = names_.at(index.row());
            return name.replace(QLatin1Char('_'), QStringLiteral("_\u200B"));
        }
        case EntryRole:
            return entry;
        case Qt::SizeHintRole:
            return itemSize_;
        case PathRole:
            return fs_->index().entries[size_t(entry)].name;
        case Qt::ToolTipRole: {
            QString tip = fs_->index().entries[size_t(entry)].name;
            if (const auto it = info_.constFind(entry); it != info_.constEnd()) {
                tip += QLatin1Char('\n') + *it;
            }
            return tip;
        }
        case Qt::DecorationRole: {
            if (const QPixmap* pixmap = cache_.object(entry)) {
                return QIcon(*pixmap);
            }
            if (failed_.contains(entry)) {
                return noTextures_.contains(entry) ? noTexture_ : failedIcon_;
            }
            // Only items the view actually draws get here, so this loads what is on screen.
            loader_->request(entry);
            return loading_;
        }
        default:
            return {};
        }
    }

private:
    void loaded(int entry, const QImage& thumbnail, const QString& info, bool hasTexture) {
        const auto row = rowOf_.constFind(entry);
        if (row == rowOf_.constEnd()) {
            return;
        }
        info_.insert(entry, info);
        if (thumbnail.isNull()) {
            failed_.insert(entry);
            if (!hasTexture) {
                noTextures_.insert(entry);
            }
        } else {
            auto* pixmap = new QPixmap(QPixmap::fromImage(thumbnail));
            const int kilobytes =
                std::max(1, int(qint64(pixmap->width()) * pixmap->height() * 4 / 1024));
            cache_.insert(entry, pixmap, kilobytes);
            if (!seen_.contains(entry)) {
                seen_.insert(entry);
                ++loadedCount_;
            }
        }
        const QModelIndex index = this->index(*row);
        emit dataChanged(index, index, {Qt::DecorationRole, Qt::ToolTipRole});
    }

    std::shared_ptr<const EvoFileSystem> fs_;
    std::vector<int> packs_;
    QStringList names_;
    QHash<int, int> rowOf_;
    ThumbnailLoader* loader_;
    // Thumbnails, least recently used dropped first; dropped ones reload when seen again.
    mutable QCache<int, QPixmap> cache_;
    QHash<int, QString> info_;
    QSet<int> failed_;
    QSet<int> noTextures_;
    QSet<int> seen_;
    int loadedCount_ = 0;
    QSize itemSize_;
    QIcon loading_;
    QIcon failedIcon_;
    QIcon noTexture_;
};

GalleryWindow::GalleryWindow(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> packs,
                             const QString& title, Actions actions, QWidget* parent)
    : QWidget(parent, Qt::Window), fs_(fs), actions_(std::move(actions)), title_(title) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("%1 - Gallery").arg(title));
    resize(1100, 760);

    // Alphabetical by name, like a file browser.
    const auto& entries = fs_->index().entries;
    std::sort(packs.begin(), packs.end(), [&entries](int a, int b) {
        return entries[size_t(a)].name.compare(entries[size_t(b)].name, Qt::CaseInsensitive) < 0;
    });

    model_ = new GalleryModel(fs_, std::move(packs), this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterRole(PathRole);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Filter names"));
    filter_->setClearButtonEnabled(true);
    size_ = new QSlider(Qt::Horizontal);
    size_->setRange(48, ThumbnailSize);
    size_->setValue(QSettings().value("gallery/size", 112).toInt());
    size_->setFixedWidth(140);
    count_ = new QLabel;
    export_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
                              tr("Export Textures..."));
    export_->setToolTip(tr("Export the selected textures, or all shown when none is selected"));

    view_ = new QListView;
    view_->setModel(proxy_);
    view_->setViewMode(QListView::IconMode);
    view_->setResizeMode(QListView::Adjust);
    view_->setMovement(QListView::Static);
    view_->setUniformItemSizes(true);
    view_->setWordWrap(true);
    view_->setTextElideMode(Qt::ElideMiddle);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setSpacing(4);

    auto* bar = new QHBoxLayout;
    bar->addWidget(filter_, 1);
    bar->addWidget(new QLabel(tr("Size")));
    bar->addWidget(size_);
    bar->addWidget(count_);
    bar->addWidget(export_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(view_, 1);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& text) {
        proxy_->setFilterFixedString(text.trimmed());
        updateCount();
    });
    connect(size_, &QSlider::valueChanged, this, &GalleryWindow::applyIconSize);
    connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex& index) {
        if (actions_.openPack) {
            actions_.openPack(index.data(EntryRole).toInt());
        }
    });
    connect(view_, &QWidget::customContextMenuRequested, this, &GalleryWindow::showContextMenu);
    connect(export_, &QPushButton::clicked, this, [this] {
        if (actions_.exportTextures) {
            actions_.exportTextures(selectedPacks(), title_);
        }
    });
    connect(model_, &QAbstractItemModel::dataChanged, this, &GalleryWindow::updateCount);

    applyIconSize(size_->value());
    updateCount();
}

void GalleryWindow::applyIconSize(int size) {
    view_->setIconSize(QSize(size, size));
    const QSize cell(size + 56, size + 2 * fontMetrics().lineSpacing() + 12);
    model_->setItemSize(cell - QSize(8, 4));
    view_->setGridSize(cell);
    QSettings().setValue("gallery/size", size);
}

void GalleryWindow::updateCount() {
    count_->setText(tr("%1 of %2 shown, %3 loaded")
                        .arg(proxy_->rowCount())
                        .arg(model_->rowCount())
                        .arg(model_->loadedCount()));
}

std::vector<int> GalleryWindow::selectedPacks() const {
    std::vector<int> out;
    const QModelIndexList selected = view_->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        for (int r = 0; r < proxy_->rowCount(); ++r) {
            out.push_back(proxy_->index(r, 0).data(EntryRole).toInt());
        }
        return out;
    }
    for (const QModelIndex& index : selected) {
        out.push_back(index.data(EntryRole).toInt());
    }
    return out;
}

void GalleryWindow::showContextMenu(const QPoint& pos) {
    const QModelIndex at = view_->indexAt(pos);
    QMenu menu(this);
    if (at.isValid() && actions_.openPack) {
        const int entry = at.data(EntryRole).toInt();
        menu.addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Resource Pack"),
                       this, [this, entry] { actions_.openPack(entry); });
    }
    menu.addAction(export_->icon(), export_->text(), export_, &QPushButton::click);
    if (at.isValid()) {
        menu.addAction(tr("Copy Path"), this, [this] {
            QStringList paths;
            for (const QModelIndex& index : view_->selectionModel()->selectedIndexes()) {
                paths << index.data(PathRole).toString();
            }
            QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        });
    }
    menu.exec(view_->viewport()->mapToGlobal(pos));
}
