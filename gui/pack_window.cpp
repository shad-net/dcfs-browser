// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pack_window.h"

#include "file_tree_model.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr qint64 HexPreviewBytes = 64 * 1024;
constexpr int IndexRole = Qt::UserRole + 1;
enum Column { NameColumn, TypeColumn, SizeColumn, UidColumn };
enum Tab { ImageTab, TextTab, HexTab };
enum Channels { ChannelsRgba, ChannelsRgb, ChannelsR, ChannelsG, ChannelsB, ChannelsA };

QString uidText(quint64 uid) {
    return QStringLiteral("%1").arg(uid, 16, 16, QLatin1Char('0')).toUpper();
}

// Shows one channel as grey, or the colour with or without alpha (alpha over a checkerboard).
QImage channelView(const QImage& source, int channels) {
    if (channels == ChannelsRgba) {
        QImage out(source.size(), QImage::Format_RGB32);
        QPainter painter(&out);
        const int cell = 8;
        for (int y = 0; y < out.height(); y += cell) {
            for (int x = 0; x < out.width(); x += cell) {
                painter.fillRect(x, y, cell, cell,
                                 ((x / cell + y / cell) % 2) ? QColor(0xcc, 0xcc, 0xcc)
                                                             : QColor(0xff, 0xff, 0xff));
            }
        }
        painter.drawImage(0, 0, source);
        return out;
    }
    QImage out = source.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < out.height(); ++y) {
        uchar* p = out.scanLine(y);
        for (int x = 0; x < out.width(); ++x, p += 4) {
            if (channels == ChannelsRgb) {
                p[3] = 255;
                continue;
            }
            const uchar v = p[channels == ChannelsR   ? 0
                              : channels == ChannelsG ? 1
                              : channels == ChannelsB ? 2
                                                      : 3];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    return out;
}

QTableWidgetItem* cell(const QString& text, int index) {
    auto* item = new QTableWidgetItem(text);
    item->setData(IndexRole, index);
    return item;
}

} // namespace

PackWindow::PackWindow(std::unique_ptr<PackContext> context, QWidget* parent)
    : QMainWindow(parent), context_(std::move(context)) {
    setAttribute(Qt::WA_DeleteOnClose);
    const QString fileName = QFileInfo(context_->path()).fileName();
    setWindowTitle(tr("%1 - Resource Pack").arg(fileName));
    resize(1150, 720);
    QSettings settings;

    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    typeFilter_ = new QComboBox;
    nameFilter_ = new QLineEdit;
    nameFilter_->setPlaceholderText(tr("Filter names"));
    nameFilter_->setClearButtonEnabled(true);
    png_ = new QCheckBox(tr("Also PNG"));
    png_->setToolTip(tr("Save textures as PNG next to the DDS"));
    png_->setChecked(settings.value("pack/png", false).toBool());
    other_ = new QCheckBox(tr("Other types as raw"));
    other_->setToolTip(tr("Also save meshes, materials and other resources as raw data "
                          "(.TYPE.bin); otherwise only textures, XML and BIN are written"));
    other_->setChecked(settings.value("pack/other", false).toBool());
    exportSelected_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                      tr("Export Selected..."));
    exportVisible_ = new QPushButton(tr("Export All Shown..."));

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Type")));
    bar->addWidget(typeFilter_);
    bar->addWidget(nameFilter_, 1);
    bar->addWidget(png_);
    bar->addWidget(other_);
    bar->addWidget(exportSelected_);
    bar->addWidget(exportVisible_);

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({tr("Name"), tr("Type"), tr("Size"), tr("UID")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    for (int c = TypeColumn; c <= UidColumn; ++c) {
        table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }

    details_ = new QLabel;
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    channels_ = new QComboBox;
    channels_->addItems({tr("RGBA"), tr("RGB"), tr("Red"), tr("Green"), tr("Blue"), tr("Alpha")});
    fit_ = new QCheckBox(tr("Fit"));
    fit_->setChecked(true);
    imageView_ = new QLabel;
    imageView_->setAlignment(Qt::AlignCenter);
    imageArea_ = new QScrollArea;
    imageArea_->setWidget(imageView_);
    imageArea_->setAlignment(Qt::AlignCenter);
    imageArea_->viewport()->installEventFilter(this);
    imageTab_ = new QWidget;
    auto* imageLayout = new QVBoxLayout(imageTab_);
    imageLayout->setContentsMargins(0, 4, 0, 0);
    auto* imageBar = new QHBoxLayout;
    imageBar->addWidget(new QLabel(tr("Channels")));
    imageBar->addWidget(channels_);
    imageBar->addWidget(fit_);
    imageBar->addStretch(1);
    imageLayout->addLayout(imageBar);
    imageLayout->addWidget(imageArea_, 1);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    text_ = new QPlainTextEdit;
    text_->setReadOnly(true);
    text_->setFont(mono);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    hex_ = new QPlainTextEdit;
    hex_->setReadOnly(true);
    hex_->setFont(mono);
    hex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tabs_ = new QTabWidget;
    tabs_->addTab(imageTab_, tr("Texture"));
    tabs_->addTab(text_, tr("Text"));
    tabs_->addTab(hex_, tr("Hex"));

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(4, 0, 0, 0);
    rightLayout->addWidget(details_);
    rightLayout->addWidget(tabs_, 1);

    auto* splitter = new QSplitter;
    splitter->addWidget(table_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->addWidget(summary_);
    layout->addLayout(bar);
    layout->addWidget(splitter, 1);
    setCentralWidget(root);

    connect(typeFilter_, &QComboBox::currentIndexChanged, this, &PackWindow::applyFilter);
    connect(nameFilter_, &QLineEdit::textChanged, this, &PackWindow::applyFilter);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &PackWindow::showSelection);
    connect(channels_, &QComboBox::currentIndexChanged, this, &PackWindow::updateImage);
    connect(fit_, &QCheckBox::toggled, this, &PackWindow::updateImage);
    connect(png_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("pack/png", on); });
    connect(other_, &QCheckBox::toggled, this,
            [](bool on) { QSettings().setValue("pack/other", on); });
    connect(exportSelected_, &QPushButton::clicked, this,
            [this] { exportResources(selectedResources(), tr("selected resources")); });
    connect(exportVisible_, &QPushButton::clicked, this,
            [this] { exportResources(visibleResources(), tr("resources shown")); });

    fillTable();
    showSelection();
}

PackWindow::~PackWindow() = default;

void PackWindow::fillTable() {
    const ResourcePack& pack = context_->pack();
    const auto& resources = pack.resources();

    QMap<quint16, int> types;
    int textures = 0;
    for (const ResourceInfo& info : resources) {
        ++types[info.type()];
        textures += info.type() == quint16(ResourceType::PixelBuffer) ? 1 : 0;
    }
    typeFilter_->blockSignals(true);
    typeFilter_->addItem(tr("All types (%1)").arg(resources.size()), -1);
    for (auto it = types.cbegin(); it != types.cend(); ++it) {
        typeFilter_->addItem(
            QStringLiteral("%1 (%2)").arg(resourceTypeName(it.key())).arg(it.value()),
            int(it.key()));
    }
    typeFilter_->blockSignals(false);

    QString text = tr("<b>%1</b> &middot; %2 resources, %3 textures &middot; %4 &middot; pack "
                      "version 0x%5")
                       .arg(QFileInfo(context_->path()).fileName().toHtmlEscaped())
                       .arg(resources.size())
                       .arg(textures)
                       .arg(formatSize(quint64(pack.byteSize())))
                       .arg(pack.version(), 0, 16);
    if (pack.bigEndian()) {
        text += tr(" &middot; big-endian (PS3)");
    }
    const QStringList topMips = context_->topMipsPacks();
    if (!topMips.isEmpty()) {
        QStringList names;
        for (const QString& p : topMips) {
            names << QFileInfo(p).fileName().toHtmlEscaped();
        }
        text += tr("<br>Top mips from %1").arg(names.join(QStringLiteral(", ")));
    }
    summary_->setText(text);

    table_->setSortingEnabled(false);
    table_->setRowCount(int(resources.size()));
    for (int i = 0; i < int(resources.size()); ++i) {
        const ResourceInfo& info = resources[size_t(i)];
        table_->setItem(i, NameColumn, cell(info.displayName(), i));
        table_->setItem(i, TypeColumn, cell(resourceTypeName(info.type()), i));
        auto* size = new QTableWidgetItem;
        size->setData(Qt::DisplayRole, qint64(info.size));
        size->setData(IndexRole, i);
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, SizeColumn, size);
        table_->setItem(i, UidColumn, cell(uidText(info.uid), i));
    }
    table_->horizontalHeader()->setSortIndicator(NameColumn, Qt::AscendingOrder);
    table_->setSortingEnabled(true);
    // Textures are what people usually come for: pick the first one.
    for (int r = 0; r < table_->rowCount(); ++r) {
        if (table_->item(r, TypeColumn)->text() == QLatin1String("PIXEL_BUFFER")) {
            table_->selectRow(r);
            break;
        }
    }
    applyFilter();
}

void PackWindow::applyFilter() {
    const int type = typeFilter_->currentData().toInt();
    const QString text = nameFilter_->text().trimmed();
    const auto& resources = context_->pack().resources();
    int shown = 0;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const ResourceInfo& info =
            resources[size_t(table_->item(r, NameColumn)->data(IndexRole).toInt())];
        const bool visible =
            (type < 0 || info.type() == type) &&
            (text.isEmpty() || info.displayName().contains(text, Qt::CaseInsensitive));
        table_->setRowHidden(r, !visible);
        shown += visible ? 1 : 0;
    }
    exportVisible_->setEnabled(shown > 0);
    statusBar()->showMessage(tr("%1 of %2 resources shown").arg(shown).arg(resources.size()));
}

std::vector<int> PackWindow::selectedResources() const {
    std::vector<int> out;
    for (const QModelIndex& row : table_->selectionModel()->selectedRows(NameColumn)) {
        if (!table_->isRowHidden(row.row())) {
            out.push_back(row.data(IndexRole).toInt());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> PackWindow::visibleResources() const {
    std::vector<int> out;
    for (int r = 0; r < table_->rowCount(); ++r) {
        if (!table_->isRowHidden(r)) {
            out.push_back(table_->item(r, NameColumn)->data(IndexRole).toInt());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void PackWindow::showSelection() {
    const std::vector<int> selected = selectedResources();
    exportSelected_->setEnabled(!selected.empty());
    image_ = QImage();
    imageView_->clear();
    text_->clear();
    hex_->clear();
    tabs_->setTabEnabled(ImageTab, false);
    tabs_->setTabEnabled(TextTab, false);
    if (selected.size() != 1) {
        details_->setText(selected.empty() ? tr("Select a resource to preview it.")
                                           : tr("%1 resources selected.").arg(selected.size()));
        return;
    }

    const ResourcePack& pack = context_->pack();
    const ResourceInfo& info = pack.resources()[size_t(selected.front())];
    QString text = tr("<b>%1</b><br>%2 &middot; uid %3 &middot; %4")
                       .arg(info.displayName().toHtmlEscaped(), resourceTypeName(info.type()),
                            uidText(info.uid), formatSize(info.size));
    if (info.names.size() > 1) {
        text += tr("<br>Also named: %1")
                    .arg(info.names.mid(1).join(QStringLiteral(", ")).toHtmlEscaped());
    }
    if (!info.sourcePaths.isEmpty()) {
        text +=
            tr("<br>Source: %1").arg(info.sourcePaths.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    if (!info.dependencies.empty()) {
        text += tr("<br>%1 dependencies").arg(info.dependencies.size());
    }
    details_->setText(text);
    hex_->setPlainText(hexDump(pack.raw(info).left(HexPreviewBytes)));

    switch (ResourceType(info.type())) {
    case ResourceType::PixelBuffer:
        showTexture(info);
        return;
    case ResourceType::Xml:
    case ResourceType::Bin: {
        QByteArray blob;
        QString error;
        if (pack.readBlob(info, &blob, &error)) {
            hex_->setPlainText(hexDump(blob.left(HexPreviewBytes)));
            const bool printable =
                std::none_of(blob.begin(), blob.begin() + std::min<qsizetype>(blob.size(), 4096),
                             [](char c) { return c == '\0'; });
            if (ResourceType(info.type()) == ResourceType::Xml || (printable && !blob.isEmpty())) {
                text_->setPlainText(QString::fromUtf8(blob.left(4 * 1024 * 1024)));
                tabs_->setTabEnabled(TextTab, true);
                tabs_->setCurrentIndex(TextTab);
                return;
            }
        }
        tabs_->setCurrentIndex(HexTab);
        return;
    }
    default:
        tabs_->setCurrentIndex(HexTab);
        return;
    }
}

void PackWindow::showTexture(const ResourceInfo& info) {
    QString error;
    const auto pb = context_->pack().readPixelBuffer(info, &error);
    if (!pb) {
        details_->setText(
            details_->text() +
            tr("<br><span style=\"color:#c02020\">%1</span>").arg(error.toHtmlEscaped()));
        tabs_->setCurrentIndex(HexTab);
        return;
    }
    QString text = details_->text() + tr("<br>%1 x %2 &middot; %3 &middot; %4 mips")
                                          .arg(pb->width)
                                          .arg(pb->height)
                                          .arg(evoPixelFormatName(pb->pixelFormat))
                                          .arg(pb->mipLevels);
    if (const TextureFormat* format = findTextureFormat(pb->pixelFormat)) {
        text += tr(" &middot; DDS as %1").arg(QString::fromLatin1(format->name));
    }
    if (pb->startMip != 0) {
        text += tr(" &middot; full size from top mips");
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto texture = context_->texture(info, &error);
    if (texture) {
        image_ = textureImage(*texture);
    }
    QApplication::restoreOverrideCursor();
    if (!texture) {
        text += tr("<br><span style=\"color:#c02020\">%1</span>").arg(error.toHtmlEscaped());
        details_->setText(text);
        tabs_->setCurrentIndex(HexTab);
        return;
    }
    if (!texture->warning.isEmpty()) {
        text +=
            tr("<br><span style=\"color:#c02020\">%1</span>").arg(texture->warning.toHtmlEscaped());
    }
    details_->setText(text);
    tabs_->setTabEnabled(ImageTab, true);
    tabs_->setCurrentIndex(ImageTab);
    updateImage();
}

void PackWindow::updateImage() {
    if (image_.isNull()) {
        imageView_->clear();
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(channelView(image_, channels_->currentIndex()));
    if (fit_->isChecked()) {
        const QSize area = imageArea_->viewport()->size() - QSize(4, 4);
        if (pixmap.width() > area.width() || pixmap.height() > area.height()) {
            pixmap = pixmap.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } else if (pixmap.width() * 2 <= area.width() && pixmap.height() * 2 <= area.height()) {
            // Small textures: enlarge by whole steps so pixels stay sharp.
            const int factor =
                std::min(area.width() / pixmap.width(), area.height() / pixmap.height());
            pixmap =
                pixmap.scaled(pixmap.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        imageArea_->setWidgetResizable(true);
    } else {
        imageArea_->setWidgetResizable(false);
        imageView_->resize(pixmap.size());
    }
    imageView_->setPixmap(pixmap);
}

bool PackWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == imageArea_->viewport() && event->type() == QEvent::Resize && fit_->isChecked() &&
        !image_.isNull()) {
        updateImage();
    }
    return QMainWindow::eventFilter(watched, event);
}

void PackWindow::exportResources(std::vector<int> resources, const QString& what) {
    if (resources.empty()) {
        return;
    }
    QSettings settings;
    const QString defaultDir =
        QDir(settings.value("pack/exportDir", QDir::homePath()).toString())
            .filePath(QFileInfo(context_->path()).completeBaseName() + QStringLiteral("_rpk"));
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Export %1 (%2) to").arg(what).arg(resources.size()), defaultDir);
    if (folder.isEmpty()) {
        return;
    }
    settings.setValue("pack/exportDir", QFileInfo(folder).absolutePath());

    ResourceExportOptions options;
    options.png = png_->isChecked();
    options.other = other_->isChecked();
    QProgressDialog progress(tr("Exporting..."), tr("Cancel"), 0, int(resources.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    const auto& all = context_->pack().resources();
    QSet<QString> used;
    QStringList errors;
    int written = 0;
    int skipped = 0;
    for (size_t n = 0; n < resources.size(); ++n) {
        if (progress.wasCanceled()) {
            break;
        }
        const ResourceInfo& info = all[size_t(resources[n])];
        progress.setValue(int(n));
        progress.setLabelText(info.displayName());
        bool wasSkipped = false;
        QString error;
        if (!context_->exportResource(info, folder, options, &used, &wasSkipped, &error)) {
            errors << QStringLiteral("%1: %2").arg(info.displayName(), error);
        } else if (wasSkipped) {
            ++skipped;
        } else {
            ++written;
        }
    }
    progress.setValue(int(resources.size()));

    QString text = tr("Exported %1 resources.").arg(written);
    if (skipped > 0) {
        text += QLatin1Char(' ') +
                (other_->isChecked()
                     ? tr("%1 were empty and skipped.").arg(skipped)
                     : tr("%1 of other types were skipped (tick \"Other types as raw\" to save "
                          "them).")
                           .arg(skipped));
    }
    if (!errors.isEmpty()) {
        text += QLatin1Char(' ') + tr("%1 failed.").arg(errors.size());
    }
    statusBar()->showMessage(text);
    QMessageBox box(errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                    tr("Export"), text, QMessageBox::Close, this);
    if (!errors.isEmpty()) {
        box.setDetailedText(errors.join(QLatin1Char('\n')));
    }
    QPushButton* show = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == show) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }
}
