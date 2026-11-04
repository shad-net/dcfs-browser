// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "main_window.h"

#include "evo_reader.h"
#include "extract_job.h"
#include "file_list.h"
#include "file_tree_model.h"
#include "gallery_window.h"
#include "pack_window.h"
#include "resource_export.h"
#include "text_table.h"
#include "texture_batch.h"
#include "texture_export_job.h"
#include "texture_previewer.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtEndian>

#include <algorithm>

namespace {

constexpr int MaxRecent = 8;
constexpr size_t MaxAutoExpandFiles = 5000;
constexpr qint64 HexPreviewBytes = 64 * 1024;
constexpr qint64 TextPreviewBytes = 1024 * 1024;
constexpr qint64 ImagePreviewBytes = 32 * 1024 * 1024;
constexpr qint64 TablePreviewBytes = 16 * 1024 * 1024;
constexpr int MaxTableRows = 100000;
enum PreviewTab { HexTab, TextTab, ImageTab, TableTab };

QString number(qint64 value) {
    return QLocale().toString(value);
}

bool isEvoStream(const QByteArray& head) {
    return head.startsWith("EVOSLITL") || head.startsWith("EVOSBIG ");
}

QString evoStreamNote(const QByteArray& head, const QString& suffix) {
    const bool little = head.startsWith("EVOSLITL");
    QString note = little ? QObject::tr(" Evo binary stream (little-endian).")
                          : QObject::tr(" Evo binary stream (big-endian, PS3).");
    if (suffix == QLatin1String("rpk") || head.size() < 20) {
        return note;
    }
    const uchar* p = reinterpret_cast<const uchar*>(head.constData());
    const quint64 uid = little ? qFromLittleEndian<quint64>(p + 8) : qFromBigEndian<quint64>(p + 8);
    const quint32 version =
        little ? qFromLittleEndian<quint32>(p + 16) : qFromBigEndian<quint32>(p + 16);
    const quint16 type = quint16(uid >> 48);
    if (type >= 1 && type <= 76 && !resourceTypeName(type).startsWith(QLatin1String("TYPE_"))) {
        note.chop(1);
        note += QObject::tr(": looks like a %1 resource, version 0x%2.")
                    .arg(resourceTypeName(type))
                    .arg(version, 0, 16);
    }
    return note;
}

bool looksLikeText(const QByteArray& head, const QString& suffix) {
    if (isEvoStream(head)) {
        return false; // binary, whatever the name says (many .txt files are)
    }
    static const QStringList textSuffixes = {
        QStringLiteral("xml"),  QStringLiteral("txt"),  QStringLiteral("ini"),
        QStringLiteral("json"), QStringLiteral("lua"),  QStringLiteral("cfg"),
        QStringLiteral("csv"),  QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("hlsl"), QStringLiteral("fx"),   QStringLiteral("log")};
    if (textSuffixes.contains(suffix)) {
        return true;
    }
    if (head.isEmpty()) {
        return false;
    }
    if (looksLikeUnicodeText(head)) {
        return true;
    }
    const QByteArray sample = head.left(4096);
    int printable = 0;
    for (const char ch : sample) {
        const uchar c = uchar(ch);
        if (c == 0) {
            return false;
        }
        if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t' || c >= 0x80) {
            ++printable;
        }
    }
    return printable * 100 >= int(sample.size()) * 97;
}

QString fourCC(const QByteArray& head) {
    QString out;
    for (int i = 0; i < 4 && i < head.size(); ++i) {
        const uchar c = uchar(head.at(i));
        out += (c >= 0x20 && c < 0x7F) ? QChar(c) : QLatin1Char('.');
    }
    return out;
}

// "*" and "?" wildcards anywhere in the path; plain text is a substring match.
QRegularExpression wildcardFilter(const QString& text) {
    QString pattern;
    for (const QChar c : text) {
        if (c == QLatin1Char('*')) {
            pattern += QLatin1String(".*");
        } else if (c == QLatin1Char('?')) {
            pattern += QLatin1Char('.');
        } else {
            pattern += QRegularExpression::escape(QString(c));
        }
    }
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

QLabel* detailLabel() {
    auto* label = new QLabel;
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Driveclub FS Browser %1").arg(Driveclub::Version()));
    resize(1200, 760);
    setAcceptDrops(true);
    auto* versionLabel = new QLabel(tr("dcfs-browser %1").arg(Driveclub::Version()));
    versionLabel->setToolTip(tr("Built %1").arg(Driveclub::BuildTimestamp().replace('T', ' ')));
    statusBar()->addPermanentWidget(versionLabel);

    QSettings settings;
    QStyle* s = style();

    openFile_ =
        new QAction(s->standardIcon(QStyle::SP_DialogOpenButton), tr("&Open File..."), this);
    openFile_->setShortcut(QKeySequence::Open);
    openFile_->setToolTip(tr("Open game.ndx, game.dat or a gameNNN.dat"));
    openFolder_ = new QAction(s->standardIcon(QStyle::SP_DirOpenIcon), tr("Open &Folder..."), this);
    openFolder_->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    extractSelected_ =
        new QAction(s->standardIcon(QStyle::SP_DialogSaveButton), tr("Extract &Selected..."), this);
    extractSelected_->setShortcut(QKeySequence(tr("Ctrl+E")));
    extractVisible_ = new QAction(tr("Extract &Filtered..."), this);
    extractVisible_->setToolTip(tr("Extract every file the current filters show"));
    extractAll_ = new QAction(s->standardIcon(QStyle::SP_DriveHDIcon), tr("Extract &All..."), this);
    extractAll_->setShortcut(QKeySequence(tr("Ctrl+Shift+E")));
    exportList_ = new QAction(tr("Export File &Names..."), this);
    exportList_->setShortcut(QKeySequence(tr("Ctrl+L")));
    exportList_->setToolTip(tr("Save the names of the files shown (current data file and filter)"));
    exportTextures_ = new QAction(tr("Export &Textures..."), this);
    exportTextures_->setShortcut(QKeySequence(tr("Ctrl+T")));
    exportTextures_->setToolTip(tr("Save every texture inside the resource packs of the selection "
                                   "(or of everything shown) as DDS and/or PNG"));
    connect(exportTextures_, &QAction::triggered, this,
            [this] { exportTextures(fs_, selectionOrVisible(), tr("the selection")); });
    exportPerData_ = new QAction(tr("Export Names per &Data File..."), this);
    exportPerData_->setToolTip(tr("Save one list per gameNNN.dat into a folder"));
    scanFolder_ = new QAction(s->standardIcon(QStyle::SP_FileDialogContentsView),
                              tr("&Scan Folder..."), this);
    scanFolder_->setShortcut(QKeySequence(tr("Ctrl+Shift+F")));
    scanFolder_->setToolTip(tr("Search a folder and all its subfolders for Driveclub archives"));
    auto* quit = new QAction(tr("&Quit"), this);
    quit->setShortcut(QKeySequence::Quit);

    connect(openFile_, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(openFolder_, &QAction::triggered, this, &MainWindow::openFolderDialog);
    connect(extractSelected_, &QAction::triggered, this, &MainWindow::extractSelected);
    connect(extractVisible_, &QAction::triggered, this, &MainWindow::extractVisible);
    connect(extractAll_, &QAction::triggered, this, &MainWindow::extractAll);
    connect(exportList_, &QAction::triggered, this, &MainWindow::exportFileList);
    connect(exportPerData_, &QAction::triggered, this, [this] { exportListsPerDataFile(fs_); });
    connect(scanFolder_, &QAction::triggered, this, &MainWindow::scanFolderDialog);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(openFile_);
    fileMenu->addAction(openFolder_);
    fileMenu->addAction(scanFolder_);
    recentMenu_ = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(exportList_);
    fileMenu->addAction(exportPerData_);
    fileMenu->addSeparator();
    fileMenu->addAction(quit);
    QMenu* extractMenu = menuBar()->addMenu(tr("&Extract"));
    extractMenu->addAction(extractSelected_);
    extractMenu->addAction(extractVisible_);
    extractMenu->addAction(extractAll_);
    extractMenu->addSeparator();
    extractMenu->addAction(exportTextures_);
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    galleryAction_ = viewMenu->addAction(tr("Show as &Gallery"), this, &MainWindow::showGallery);
    galleryAction_->setShortcut(QKeySequence(tr("Ctrl+G")));
    galleryAction_->setToolTip(tr("Thumbnails of the textures in the selected folder (or "
                                  "everything shown)"));
    viewMenu->addSeparator();
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this] {
        QMessageBox::about(
            this, tr("About dcfs-browser"),
            tr("<b>dcfs-browser %1</b><p>Browse and extract Driveclub's game.ndx / gameNNN.dat "
               "archives.</p><p>File system format from <a "
               "href=\"https://github.com/Nenkai/DriveClubFS\">DriveClubFS</a> by Nenkai.</p>")
                .arg(Driveclub::Version()));
    });

    dataFilter_ = new QComboBox;
    dataFilter_->setMinimumContentsLength(28);
    dataFilter_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    filter_ = new QLineEdit;
    filter_->setPlaceholderText(tr("Filter paths (* and ? wildcards)"));
    filter_->setClearButtonEnabled(true);
    filterTimer_ = new QTimer(this);
    filterTimer_->setSingleShot(true);
    filterTimer_->setInterval(250);
    verify_ = new QCheckBox(tr("Verify MD5"));
    verify_->setToolTip(tr("Check every extracted file against the MD5 stored in game.ndx"));
    verify_->setChecked(settings.value("verify", true).toBool());

    auto* bar = new QToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addAction(openFile_);
    bar->addAction(openFolder_);
    bar->addAction(scanFolder_);
    bar->addSeparator();
    bar->addAction(extractSelected_);
    bar->addAction(extractAll_);
    bar->addSeparator();
    auto* barWidget = new QWidget;
    auto* barLayout = new QHBoxLayout(barWidget);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->addWidget(new QLabel(tr("Data file")));
    barLayout->addWidget(dataFilter_);
    barLayout->addWidget(filter_, 1);
    barLayout->addWidget(verify_);
    bar->addWidget(barWidget);
    addToolBar(bar);

    connect(dataFilter_, &QComboBox::currentIndexChanged, this, &MainWindow::applyDataFilter);
    connect(filter_, &QLineEdit::textChanged, filterTimer_, qOverload<>(&QTimer::start));
    connect(filterTimer_, &QTimer::timeout, this, &MainWindow::applyTextFilter);
    connect(verify_, &QCheckBox::toggled, this,
            [](bool on) { QSettings().setValue("verify", on); });

    model_ = new FileTreeModel(this);
    proxy_ = new FileTreeProxy(this);
    proxy_->setSourceModel(model_);

    tree_ = new QTreeView;
    tree_->setModel(proxy_);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setAlternatingRowColors(true);
    tree_->setSortingEnabled(true);
    tree_->sortByColumn(FileTreeModel::NameColumn, Qt::AscendingOrder);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(FileTreeModel::NameColumn, QHeaderView::Stretch);
    for (int c = FileTreeModel::SizeColumn; c < FileTreeModel::ColumnCount; ++c) {
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    connect(tree_, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(tree_, &QTreeView::activated, this, [this](const QModelIndex& index) {
        const int entry = index.data(FileTreeModel::EntryRole).toInt();
        if (entry >= 0 && fs_->index().entries[size_t(entry)].name.endsWith(QLatin1String(".rpk"),
                                                                            Qt::CaseInsensitive)) {
            openResourcePack(entry);
        }
    });
    connect(tree_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::showSelection);
    connect(tree_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::updateActions);
    auto* copy = new QAction(this);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    connect(copy, &QAction::triggered, this, &MainWindow::copySelectedPaths);
    tree_->addAction(copy);

    summary_ = new QLabel(tr("Open a Driveclub game.ndx, a gameNNN.dat or the folder holding "
                             "them (File > Open, or drop it here)."));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_->setContentsMargins(4, 4, 4, 4);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(summary_);
    leftLayout->addWidget(tree_, 1);

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(buildDetails());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    setCentralWidget(splitter);

    archivesDock_ = buildArchivesPanel();
    addDockWidget(Qt::LeftDockWidgetArea, archivesDock_);
    archivesDock_->hide();
    viewMenu->addAction(archivesDock_->toggleViewAction());

    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    splitter->restoreState(settings.value("splitter").toByteArray());
    updateRecentMenu();
    updateActions();
    setStatus(tr("No archive open"));
}

MainWindow::~MainWindow() = default;

QWidget* MainWindow::buildDetails() {
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 0, 0, 0);

    detailsForm_ = new QFormLayout;
    detailPath_ = detailLabel();
    detailSize_ = detailLabel();
    detailData_ = detailLabel();
    detailOffset_ = detailLabel();
    detailChunks_ = detailLabel();
    detailHash_ = detailLabel();
    detailMd5_ = detailLabel();
    detailsForm_->addRow(tr("Path"), detailPath_);
    detailsForm_->addRow(tr("Size"), detailSize_);
    detailsForm_->addRow(tr("Data file"), detailData_);
    detailsForm_->addRow(tr("Offset"), detailOffset_);
    detailsForm_->addRow(tr("Chunks"), detailChunks_);
    detailsForm_->addRow(tr("Name hash"), detailHash_);
    detailsForm_->addRow(tr("MD5"), detailMd5_);
    layout->addLayout(detailsForm_);

    previewNote_ = new QLabel;
    previewNote_->setWordWrap(true);
    layout->addWidget(previewNote_);
    openPack_ =
        new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Resource Pack"));
    openPack_->setToolTip(tr("Browse the textures, XML and other resources inside (or "
                             "double-click the file)"));
    openPack_->hide();
    connect(openPack_, &QPushButton::clicked, this, [this] {
        if (currentPackEntry() >= 0) {
            openResourcePack(currentPackEntry());
        }
    });
    layout->addWidget(openPack_, 0, Qt::AlignLeft);
    showGallery_ = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                   tr("Show as Gallery"));
    showGallery_->setToolTip(tr("Thumbnails of every texture in this folder (Ctrl+G)"));
    showGallery_->hide();
    connect(showGallery_, &QPushButton::clicked, this, &MainWindow::showGallery);
    layout->addWidget(showGallery_, 0, Qt::AlignLeft);

    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    hexView_ = new QPlainTextEdit;
    hexView_->setReadOnly(true);
    hexView_->setFont(mono);
    hexView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    textView_ = new QPlainTextEdit;
    textView_->setReadOnly(true);
    textView_->setFont(mono);
    textView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    imageView_ = new QLabel;
    imageView_->setAlignment(Qt::AlignCenter);
    imageArea_ = new QScrollArea;
    imageArea_->setWidget(imageView_);
    imageArea_->setWidgetResizable(true);
    imageArea_->viewport()->installEventFilter(this);
    previewer_ = new TexturePreviewer(this);
    connect(previewer_, &TexturePreviewer::ready, this, &MainWindow::textureReady);

    previewTabs_ = new QTabWidget;
    previewTabs_->addTab(hexView_, tr("Hex"));
    previewTabs_->addTab(textView_, tr("Text"));
    previewTabs_->addTab(imageArea_, tr("Image"));

    tableFilter_ = new QLineEdit;
    tableFilter_->setPlaceholderText(tr("Filter rows"));
    tableFilter_->setClearButtonEnabled(true);
    tableInfo_ = new QLabel;
    tableView_ = new QTableWidget;
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setAlternatingRowColors(true);
    tableView_->setWordWrap(false);
    tableView_->verticalHeader()->setDefaultSectionSize(tableView_->fontMetrics().height() + 6);
    tableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    connect(tableFilter_, &QLineEdit::textChanged, this, &MainWindow::filterTable);
    auto* copyCells = new QShortcut(QKeySequence::Copy, tableView_);
    copyCells->setContext(Qt::WidgetShortcut);
    connect(copyCells, &QShortcut::activated, this, &MainWindow::copyTableCells);
    tableTab_ = new QWidget;
    auto* tableLayout = new QVBoxLayout(tableTab_);
    tableLayout->setContentsMargins(0, 4, 0, 0);
    auto* tableBar = new QHBoxLayout;
    tableBar->addWidget(tableFilter_, 1);
    tableBar->addWidget(tableInfo_);
    tableLayout->addLayout(tableBar);
    tableLayout->addWidget(tableView_, 1);
    previewTabs_->addTab(tableTab_, tr("Table"));
    previewTabs_->setTabEnabled(TableTab, false);
    previewTabs_->setTabEnabled(TextTab, false);
    previewTabs_->setTabEnabled(ImageTab, false);
    layout->addWidget(previewTabs_, 1);
    return root;
}

bool MainWindow::openPath(const QString& path) {
    if (busy()) {
        setStatus(tr("Wait for the extraction to finish first."), true);
        return false;
    }
    const QFileInfo info(path);
    if (info.isFile() && info.suffix().compare(QStringLiteral("rpk"), Qt::CaseInsensitive) == 0) {
        return openPackFile(path);
    }
    if (info.isDir() && !QFileInfo::exists(QDir(path).filePath(QStringLiteral("game.ndx"))) &&
        !QFileInfo::exists(QDir(path).filePath(QStringLiteral("game.dat")))) {
        // Not an archive folder itself: look for archives below it instead.
        scanFolder(path);
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto fs = std::make_shared<EvoFileSystem>();
    QString error;
    const bool ok = fs->open(path, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        setStatus(tr("Could not open %1").arg(QDir::toNativeSeparators(path)), true);
        QMessageBox::critical(this, tr("Open"), error);
        return false;
    }
    const int focus = fs->focusDataIndex();
    showFileSystem(std::move(fs), path, focus);
    return true;
}

void MainWindow::showFileSystem(std::shared_ptr<const EvoFileSystem> fs, const QString& path,
                                int dataIndex) {
    fs_ = std::move(fs);
    previewReader_ = std::make_unique<EvoReader>(fs_);
    filter_->blockSignals(true);
    filter_->clear();
    filter_->blockSignals(false);
    proxy_->setFilterFixedString(QString());
    model_->setFileSystem(fs_, dataIndex);
    fillDataFilter(dataIndex);
    showSelection();

    const QString shown = QFileInfo(path).absoluteFilePath();
    setWindowTitle(tr("%1 - Driveclub FS Browser").arg(QFileInfo(shown).fileName()));
    QSettings settings;
    QStringList recent = settings.value("recent").toStringList();
    recent.removeAll(shown);
    recent.prepend(shown);
    while (recent.size() > MaxRecent) {
        recent.removeLast();
    }
    settings.setValue("recent", recent);
    settings.setValue("lastDir", QFileInfo(shown).absolutePath());
    updateRecentMenu();

    setStatus(tr("Opened %1: %2")
                  .arg(QDir::toNativeSeparators(fs_->indexPath()),
                       fileCount(qint64(fs_->index().entries.size()))));
}

void MainWindow::openFileDialog() {
    const QString start = QSettings().value("lastDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Driveclub archive"), start,
        tr("Driveclub archives and resource packs (*.ndx *.dat *.rpk);;All files (*)"));
    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::openFolderDialog() {
    const QString start = QSettings().value("lastDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Open folder containing game.ndx or game.dat"), start);
    if (!path.isEmpty()) {
        openPath(path);
    }
}

void MainWindow::updateRecentMenu() {
    recentMenu_->clear();
    const QStringList recent = QSettings().value("recent").toStringList();
    for (const QString& path : recent) {
        recentMenu_->addAction(QDir::toNativeSeparators(path), this,
                               [this, path] { openPath(path); });
    }
    if (!recent.isEmpty()) {
        recentMenu_->addSeparator();
        recentMenu_->addAction(tr("Clear"), this, [this] {
            QSettings().remove("recent");
            updateRecentMenu();
        });
    }
    recentMenu_->setEnabled(!recent.isEmpty());
}

void MainWindow::fillDataFilter(int dataIndex) {
    dataFilter_->blockSignals(true);
    dataFilter_->clear();
    if (fs_) {
        const qint64 total = qint64(fs_->index().entries.size());
        dataFilter_->addItem(tr("All data files (%1 files)").arg(number(total)), -1);
        const QIcon missing = style()->standardIcon(QStyle::SP_MessageBoxWarning);
        for (int i : fs_->dataIndices()) {
            QString text = tr("%1 - %2 files").arg(fs_->dataFileName(i), number(fs_->fileCount(i)));
            const bool present = fs_->dataFile(i) != nullptr;
            if (!present) {
                text += tr(" (%1)").arg(fs_->dataFileError(i));
            }
            dataFilter_->addItem(present ? QIcon() : missing, text, i);
            if (!present) {
                dataFilter_->setItemData(dataFilter_->count() - 1,
                                         tr("Names are listed from the index, but the data file "
                                            "is %1: these files cannot be extracted.")
                                             .arg(fs_->dataFileError(i)),
                                         Qt::ToolTipRole);
            }
        }
        const int at = dataFilter_->findData(dataIndex);
        dataFilter_->setCurrentIndex(at < 0 ? 0 : at);
    }
    dataFilter_->setEnabled(fs_ && fs_->dataIndices().size() > 1);
    dataFilter_->blockSignals(false);
    applyDataFilter();
}

void MainWindow::applyDataFilter() {
    if (!fs_) {
        summary_->clear();
        return;
    }
    const int dataIndex = dataFilter_->currentData().toInt();
    model_->setDataFilter(dataIndex);
    if (model_->rowCount() == 1) {
        tree_->expand(proxy_->index(0, 0));
    }

    const EvoIndex& index = fs_->index();
    int missing = 0;
    for (int i : fs_->dataIndices()) {
        missing += fs_->dataFile(i) ? 0 : 1;
    }
    QString text =
        tr("<b>%1</b> &middot; %2 (v%3) &middot; built %4 UTC &middot; %5 "
           "compression &middot; %6 files")
            .arg(QFileInfo(fs_->indexPath()).fileName().toHtmlEscaped(),
                 fs_->isAlpha() ? tr("Driveclub alpha") : tr("Driveclub"))
            .arg(index.version)
            .arg(index.timeStamp.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                 compressionName(index.compression), number(qint64(index.entries.size())));
    if (!fs_->isAlpha()) {
        text += tr(" in %1 data files").arg(fs_->dataIndices().size());
        if (missing > 0) {
            text +=
                tr(" <span style=\"color:#c02020\">(%1 missing or unreadable)</span>").arg(missing);
        }
    }
    if (dataIndex >= 0) {
        text += tr("<br>Showing <b>%1</b>: %2 files, %3 unpacked")
                    .arg(fs_->dataFileName(dataIndex), number(model_->visibleFileCount()),
                         formatSize(model_->visibleSize()));
        if (const EvoDataFile* data = fs_->dataFile(dataIndex)) {
            text += tr(", %1 on disk in %2 chunks of %3")
                        .arg(formatSize(data->storedSize()), number(data->chunkCount()),
                             formatSize(data->bufferSize));
        }
    }
    summary_->setText(text);
    updateActions();
}

void MainWindow::applyTextFilter() {
    const QString text = filter_->text().trimmed();
    if (text.contains(QLatin1Char('*')) || text.contains(QLatin1Char('?'))) {
        proxy_->setFilterRegularExpression(wildcardFilter(text));
    } else {
        proxy_->setFilterFixedString(text);
    }
    const size_t shown = visibleEntries().size();
    if (!text.isEmpty()) {
        // Open every folder when the matches are few enough to look through; otherwise only the
        // top levels, since expanding hundreds of thousands of rows stalls the view.
        if (shown <= MaxAutoExpandFiles) {
            tree_->expandAll();
        } else {
            tree_->expandToDepth(1);
        }
    }
    setStatus(text.isEmpty() ? tr("Showing %1").arg(fileCount(qint64(shown)))
                             : tr("Filter \"%1\": %2").arg(text, fileCount(qint64(shown))));
    updateActions();
}

void MainWindow::showSelection() {
    const QModelIndexList rows = tree_->selectionModel()->selectedRows(0);
    for (QLabel* label : {detailPath_, detailSize_, detailData_, detailOffset_, detailChunks_,
                          detailHash_, detailMd5_}) {
        label->clear();
    }
    hexView_->clear();
    textView_->clear();
    imageView_->clear();
    previewNote_->clear();
    previewNote_->setStyleSheet(QString());
    openPack_->hide();
    showGallery_->hide();
    previewer_->cancel();
    previewTicket_ = -1;
    previewImage_ = QImage();
    previewTabs_->setTabEnabled(TextTab, false);
    previewTabs_->setTabEnabled(ImageTab, false);
    previewTabs_->setTabEnabled(TableTab, false);
    tableView_->clear();
    tableView_->setRowCount(0);
    tableView_->setColumnCount(0);
    tableInfo_->clear();
    if (!fs_ || rows.isEmpty()) {
        return;
    }
    if (rows.size() > 1) {
        const std::vector<int> entries = selectedEntries();
        quint64 bytes = 0;
        for (int i : entries) {
            bytes += quint64(std::max(0, fs_->index().entries[size_t(i)].size));
        }
        detailPath_->setText(tr("%1 items selected").arg(rows.size()));
        detailSize_->setText(
            tr("%1 in %2 files").arg(formatSize(bytes), number(qint64(entries.size()))));
        return;
    }

    const QModelIndex row = rows.first();
    const int entryIndex = row.data(FileTreeModel::EntryRole).toInt();
    if (entryIndex < 0) {
        const QModelIndex source = proxy_->mapToSource(row);
        detailPath_->setText(row.data(FileTreeModel::PathRole).toString() + QLatin1Char('/'));
        detailSize_->setText(source.siblingAtColumn(FileTreeModel::SizeColumn).data().toString());
        detailData_->setText(
            source.siblingAtColumn(FileTreeModel::DataFileColumn).data().toString());
        previewNote_->setText(tr("Folder. Select a file to preview it, Ctrl+E to extract "
                                 "everything inside, or Ctrl+G to see its textures as a gallery."));
        showGallery_->show();
        return;
    }
    showPreview(entryIndex);
}

void MainWindow::showPreview(int entryIndex) {
    const EvoIndexEntry& entry = fs_->index().entries[size_t(entryIndex)];
    const EvoDataFile* data = fs_->dataFile(entry.datIndex);
    detailPath_->setText(entry.name);
    detailSize_->setText(
        tr("%1 (%2 bytes)").arg(formatSize(quint64(std::max(0, entry.size))), number(entry.size)));
    detailData_->setText(QDir::toNativeSeparators(
        QDir(fs_->directory()).filePath(fs_->dataFileName(entry.datIndex))));
    detailOffset_->setText(
        QStringLiteral("0x%1 (%2)").arg(entry.offset, 0, 16).arg(number(qint64(entry.offset))));
    detailHash_->setText(
        QStringLiteral("0x") +
        QString::number(entry.nameHash, 16).toUpper().rightJustified(8, QLatin1Char('0')));
    detailMd5_->setText(entry.md5.isEmpty() ? tr("(not stored in this version)")
                                            : QString::fromLatin1(entry.md5.toHex()));
    if (data) {
        const qint64 first = qint64(entry.offset / data->bufferSize);
        const qint64 last =
            entry.size > 0 ? qint64((entry.offset + quint64(entry.size) - 1) / data->bufferSize)
                           : first;
        detailChunks_->setText(
            first == last ? tr("%1").arg(first)
                          : tr("%1 - %2 (%3 chunks)").arg(first).arg(last).arg(last - first + 1));
    } else {
        previewNote_->setStyleSheet(QStringLiteral("color: #c02020;"));
        previewNote_->setText(
            tr("%1 is %2, so this file cannot be previewed or extracted.")
                .arg(fs_->dataFileName(entry.datIndex), fs_->dataFileError(entry.datIndex)));
        return;
    }

    QString error;
    QByteArray head;
    if (!previewReader_->read(entry, 0, HexPreviewBytes, &head, &error)) {
        previewNote_->setStyleSheet(QStringLiteral("color: #c02020;"));
        previewNote_->setText(tr("Cannot read this file: %1").arg(error));
        return;
    }
    hexView_->setPlainText(hexDump(head));

    QString note = head.size() < entry.size
                       ? tr("Hex shows the first %1 of %2.")
                             .arg(formatSize(quint64(head.size())), formatSize(quint64(entry.size)))
                       : tr("Hex shows the whole file.");
    if (head.size() >= 4) {
        note += tr(" Starts with \"%1\".").arg(fourCC(head).toHtmlEscaped());
    }
    const QString suffix = QFileInfo(entry.name).suffix().toLower();
    if (isEvoStream(head)) {
        note += evoStreamNote(head, suffix);
    }

    PreviewTab best = HexTab;
    const bool csv = suffix == QLatin1String("csv") || suffix == QLatin1String("tsv");
    if (csv && entry.size <= TablePreviewBytes) {
        QByteArray bytes = head;
        if (entry.size > head.size()) {
            previewReader_->read(entry, 0, entry.size, &bytes, &error);
        }
        QString encoding;
        const QString text = decodeText(bytes, &encoding);
        note += QLatin1Char(' ') + showTable(text) + tr(", %1.").arg(encoding);
        textView_->setPlainText(text.left(int(TextPreviewBytes)));
        previewTabs_->setTabEnabled(TextTab, true);
        previewTabs_->setTabEnabled(TableTab, true);
        best = TableTab;
    } else if (looksLikeText(head, suffix)) {
        QByteArray text = head;
        if (entry.size > head.size()) {
            previewReader_->read(entry, 0, TextPreviewBytes, &text, &error);
        }
        QString encoding;
        textView_->setPlainText(decodeText(text, &encoding));
        if (encoding != QLatin1String("UTF-8")) {
            note += tr(" Text is %1.").arg(encoding);
        }
        previewTabs_->setTabEnabled(TextTab, true);
        best = TextTab;
        if (text.size() < entry.size) {
            note += tr(" Text shows the first %1.").arg(formatSize(quint64(text.size())));
        }
    }
    const bool imageType = QImageReader::supportedImageFormats().contains(suffix.toLatin1());
    if (imageType && entry.size <= ImagePreviewBytes) {
        QByteArray bytes = head;
        if (entry.size > head.size()) {
            previewReader_->read(entry, 0, entry.size, &bytes, &error);
        }
        const QImage image = QImage::fromData(bytes);
        if (!image.isNull()) {
            showPreviewImage(image);
            best = ImageTab;
            note += tr(" Image: %1 x %2.").arg(image.width()).arg(image.height());
        }
    }
    if (suffix == QLatin1String("rpk")) {
        note += tr(" Resource pack: double-click to see everything inside.");
        openPack_->show();
        if (!isTopMipsPackName(entry.name)) {
            previewBaseNote_ = note;
            note += tr(" Loading texture...");
            previewTicket_ = previewer_->request(fs_, entryIndex);
        }
    }
    previewNote_->setText(note);
    if (!previewTabs_->isTabEnabled(previewTabs_->currentIndex()) || best != HexTab) {
        previewTabs_->setCurrentIndex(best);
    }
}

void MainWindow::showContextMenu(const QPoint& pos) {
    if (!fs_) {
        return;
    }
    QMenu menu(this);
    if (const int pack = currentPackEntry(); pack >= 0) {
        menu.addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Resource Pack"),
                       this, [this, pack] { openResourcePack(pack); });
        menu.addSeparator();
    }
    menu.addAction(galleryAction_);
    menu.addAction(extractSelected_);
    menu.addAction(exportTextures_);
    menu.addAction(tr("Copy Path"), this, &MainWindow::copySelectedPaths);
    QAction* names = menu.addAction(tr("Export File Names of Selection..."), this, [this] {
        exportFileNames(fs_, selectedEntries(), tr("selection"), QStringLiteral("selection"));
    });
    names->setEnabled(tree_->selectionModel()->hasSelection());
    menu.addSeparator();
    const QModelIndex at = tree_->indexAt(pos);
    if (at.isValid() && at.data(FileTreeModel::EntryRole).toInt() < 0) {
        menu.addAction(tr("Expand Folder"), this,
                       [this, at] { tree_->expandRecursively(at.siblingAtColumn(0)); });
    }
    menu.addAction(tr("Collapse All"), tree_, &QTreeView::collapseAll);
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

int MainWindow::currentPackEntry() const {
    const QModelIndexList rows = tree_->selectionModel()->selectedRows(0);
    if (!fs_ || rows.size() != 1) {
        return -1;
    }
    const int entry = rows.first().data(FileTreeModel::EntryRole).toInt();
    if (entry < 0 || !fs_->index().entries[size_t(entry)].name.endsWith(QLatin1String(".rpk"),
                                                                        Qt::CaseInsensitive)) {
        return -1;
    }
    return entry;
}

void MainWindow::openResourcePack(int entryIndex) {
    openResourcePack(fs_, entryIndex);
}

void MainWindow::openResourcePack(const std::shared_ptr<const EvoFileSystem>& fs, int entryIndex) {
    const EvoIndexEntry& entry = fs->index().entries[size_t(entryIndex)];
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    auto context = openPackFromArchive(fs, entryIndex, &error);
    PackWindow* window = context ? new PackWindow(std::move(context)) : nullptr;
    QApplication::restoreOverrideCursor();
    if (!window) {
        setStatus(tr("Could not open %1").arg(entry.name), true);
        QMessageBox::warning(this, tr("Open resource pack"), tr("%1:\n%2").arg(entry.name, error));
        return;
    }
    window->show();
    setStatus(tr("Opened resource pack %1").arg(entry.name));
}

void MainWindow::showGallery() {
    if (!fs_) {
        return;
    }
    std::vector<int> packs = TextureBatch::packsIn(*fs_, selectionOrVisible());
    if (packs.empty()) {
        QMessageBox::information(this, tr("Gallery"),
                                 tr("There are no resource packs (.rpk) in the selection."));
        return;
    }
    // Name it after the folder when one folder is selected.
    QString title = tr("%1 packs").arg(number(qint64(packs.size())));
    const QModelIndexList rows = tree_->selectionModel()->selectedRows(0);
    if (rows.size() == 1 && rows.first().data(FileTreeModel::EntryRole).toInt() < 0) {
        title = rows.first().data(FileTreeModel::PathRole).toString();
    } else if (!filter_->text().trimmed().isEmpty() && rows.isEmpty()) {
        title = tr("filter \"%1\"").arg(filter_->text().trimmed());
    }
    GalleryWindow::Actions actions;
    const std::shared_ptr<const EvoFileSystem> fs = fs_;
    actions.openPack = [this, fs](int entry) { openResourcePack(fs, entry); };
    actions.exportTextures = [this, fs](std::vector<int> entries, const QString& what) {
        exportTextures(fs, std::move(entries), what);
    };
    auto* gallery = new GalleryWindow(fs_, std::move(packs), title, actions, this);
    gallery->show();
}

bool MainWindow::openPackFile(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    auto context = openPackFromDisk(path, &error);
    PackWindow* window = context ? new PackWindow(std::move(context)) : nullptr;
    QApplication::restoreOverrideCursor();
    if (!window) {
        QMessageBox::warning(this, tr("Open resource pack"), error);
        return false;
    }
    window->show();
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    setStatus(tr("Opened resource pack %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

void MainWindow::copySelectedPaths() {
    QStringList paths;
    for (const QModelIndex& row : tree_->selectionModel()->selectedRows(0)) {
        paths << row.data(FileTreeModel::PathRole).toString();
    }
    if (!paths.isEmpty()) {
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        setStatus(paths.size() == 1 ? tr("Copied 1 path")
                                    : tr("Copied %1 paths").arg(paths.size()));
    }
}

void MainWindow::exportFileList() {
    if (!fs_) {
        return;
    }
    const int dataIndex = dataFilter_->currentData().toInt();
    const bool filtered = !filter_->text().trimmed().isEmpty();
    QString name = dataIndex >= 0 ? QFileInfo(fs_->dataFileName(dataIndex)).completeBaseName()
                                  : QStringLiteral("files");
    QString what = dataIndex >= 0 ? fs_->dataFileName(dataIndex) : tr("all data files");
    if (filtered) {
        what += tr(", filtered");
    }
    exportFileNames(fs_, visibleEntries(), what, name);
}

void MainWindow::exportFileNames(const std::shared_ptr<const EvoFileSystem>& fs,
                                 std::vector<int> entries, const QString& what,
                                 const QString& defaultName) {
    if (!fs || entries.empty()) {
        return;
    }
    const QString namesFilter = tr("File names, one per line (*.txt)");
    const QString detailedFilter = tr("Detailed list with size and offset (*.txt)");
    const QString csvFilter = tr("CSV with size, offset, hash and MD5 (*.csv)");
    QSettings settings;
    QString selected = settings.value("exportFormat", namesFilter).toString();
    if (selected != detailedFilter && selected != csvFilter) {
        selected = namesFilter;
    }
    const QString suffix = selected == csvFilter ? QStringLiteral(".csv") : QStringLiteral(".txt");
    const QString start =
        QDir(settings.value("exportDir", QFileInfo(fs->indexPath()).absolutePath()).toString())
            .filePath(defaultName + suffix);
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export file names: %1 (%2)").arg(what, fileCount(qint64(entries.size()))), start,
        QStringList{namesFilter, detailedFilter, csvFilter}.join(QStringLiteral(";;")), &selected);
    if (path.isEmpty()) {
        return;
    }
    const FileListFormat format = selected == csvFilter        ? FileListFormat::Csv
                                  : selected == detailedFilter ? FileListFormat::Detailed
                                                               : FileListFormat::Names;
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QLatin1Char('.') + fileListSuffix(format);
    }
    settings.setValue("exportFormat", selected);
    settings.setValue("exportDir", QFileInfo(path).absolutePath());

    const qint64 count = qint64(entries.size());
    QString error;
    if (!writeFileList(*fs, std::move(entries), path, format, &error)) {
        QMessageBox::critical(this, tr("Export file names"), error);
        return;
    }
    setStatus(tr("Wrote %1 names to %2").arg(number(count), QDir::toNativeSeparators(path)));
}

void MainWindow::exportListsPerDataFile(const std::shared_ptr<const EvoFileSystem>& fs) {
    if (!fs) {
        return;
    }
    const QStringList formats = {tr("File names, one per line (.txt)"),
                                 tr("Detailed list with size and offset (.txt)"),
                                 tr("CSV with size, offset, hash and MD5 (.csv)")};
    QSettings settings;
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Export names per data file"),
        tr("Writes one list per data file (game000.dat -> game000.txt).\nFormat:"), formats,
        settings.value("exportPerDataFormat", 0).toInt(), false, &ok);
    if (!ok) {
        return;
    }
    const int formatIndex = int(formats.indexOf(choice));
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Folder for the file lists"),
        settings.value("exportDir", QFileInfo(fs->indexPath()).absolutePath()).toString());
    if (folder.isEmpty()) {
        return;
    }
    settings.setValue("exportPerDataFormat", formatIndex);
    settings.setValue("exportDir", folder);

    const FileListFormat format = formatIndex == 2   ? FileListFormat::Csv
                                  : formatIndex == 1 ? FileListFormat::Detailed
                                                     : FileListFormat::Names;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int written = writeFileListPerDataFile(*fs, folder, format, &error);
    QApplication::restoreOverrideCursor();
    if (written < 0) {
        QMessageBox::critical(this, tr("Export names per data file"), error);
        return;
    }
    setStatus(
        tr("Wrote %1 file lists to %2").arg(number(written), QDir::toNativeSeparators(folder)));
    QMessageBox box(
        QMessageBox::Information, tr("Export names per data file"),
        tr("Wrote %1 file lists to %2.").arg(number(written), QDir::toNativeSeparators(folder)),
        QMessageBox::Close, this);
    QPushButton* show = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == show) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }
}

void MainWindow::collectEntries(const QModelIndex& proxyIndex, std::vector<int>* out) const {
    const int entry = proxyIndex.data(FileTreeModel::EntryRole).toInt();
    if (proxyIndex.isValid() && entry >= 0) {
        out->push_back(entry);
        return;
    }
    const int rows = proxy_->rowCount(proxyIndex);
    for (int r = 0; r < rows; ++r) {
        collectEntries(proxy_->index(r, 0, proxyIndex), out);
    }
}

std::vector<int> MainWindow::selectedEntries() const {
    std::vector<int> out;
    for (const QModelIndex& row : tree_->selectionModel()->selectedRows(0)) {
        collectEntries(row, &out);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> MainWindow::visibleEntries() const {
    std::vector<int> out;
    collectEntries(QModelIndex(), &out);
    return out;
}

void MainWindow::extractSelected() {
    startExtraction(fs_, selectedEntries(), tr("selected files"));
}

void MainWindow::extractVisible() {
    startExtraction(fs_, visibleEntries(), tr("filtered files"));
}

void MainWindow::extractAll() {
    if (!fs_) {
        return;
    }
    std::vector<int> all(fs_->index().entries.size());
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = int(i);
    }
    startExtraction(fs_, std::move(all), tr("all files"));
}

void MainWindow::startExtraction(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> entries,
                                 const QString& what) {
    if (!fs || busy() || entries.empty()) {
        return;
    }
    // Read each data file front to back: far fewer seeks and chunk reloads.
    const auto& all = fs->index().entries;
    std::sort(entries.begin(), entries.end(), [&all](int a, int b) {
        const EvoIndexEntry& x = all[size_t(a)];
        const EvoIndexEntry& y = all[size_t(b)];
        return x.datIndex != y.datIndex ? x.datIndex < y.datIndex : x.offset < y.offset;
    });
    int unavailable = 0;
    quint64 bytes = 0;
    for (int i : entries) {
        unavailable += fs->dataFile(all[size_t(i)].datIndex) ? 0 : 1;
        bytes += quint64(std::max(0, all[size_t(i)].size));
    }

    QSettings settings;
    const QString start =
        settings.value("extractDir", QDir(fs->directory()).filePath(QStringLiteral("extracted")))
            .toString();
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Extract %1 (%2 files, %3) to")
            .arg(what, number(qint64(entries.size())), formatSize(bytes)),
        start);
    if (dir.isEmpty()) {
        return;
    }
    settings.setValue("extractDir", dir);
    if (unavailable > 0 &&
        QMessageBox::question(this, tr("Extract"),
                              tr("%1 of these are stored in missing or unreadable data files and "
                                 "will be skipped. Continue?")
                                  .arg(fileCount(unavailable))) != QMessageBox::Yes) {
        return;
    }

    job_ = new ExtractJob(fs, std::move(entries), dir, verify_->isChecked(), this);
    progress_ = makeProgress(tr("Extracting"));
    connect(progress_, &QProgressDialog::canceled, job_, &ExtractJob::cancel);
    connect(
        job_, &ExtractJob::progress, this,
        [this](int done, int total, qint64 bytesDone, qint64 bytesTotal, const QString& current) {
            const QPointer<QProgressDialog> dialog = progress_;
            if (!dialog) {
                return;
            }
            dialog->setLabelText(
                tr("%1 of %2 files, %3 of %4\n%5")
                    .arg(number(done), number(total), formatSize(quint64(bytesDone)),
                         formatSize(quint64(bytesTotal)),
                         dialog->fontMetrics().elidedText(current, Qt::ElideMiddle, 480)));
            // Last: nothing may touch the dialog after this, in case it handled events.
            dialog->setValue(bytesTotal > 0 ? int(bytesDone * 1000 / bytesTotal) : 0);
        });
    connect(job_, &ExtractJob::finished, this, &MainWindow::extractionFinished);
    updateActions();
    setStatus(tr("Extracting to %1...").arg(QDir::toNativeSeparators(dir)));
    job_->start();
}

void MainWindow::extractionFinished(int extracted, const QStringList& errors, bool cancelled) {
    const QString dir = job_->outputDir();
    job_->deleteLater();
    job_ = nullptr;
    if (progress_) {
        progress_->hide();
        progress_->deleteLater();
        progress_ = nullptr;
    }
    updateActions();

    QString text = cancelled ? tr("Cancelled after extracting %1.").arg(fileCount(extracted))
                             : tr("Extracted %1.").arg(fileCount(extracted));
    if (!errors.isEmpty()) {
        text += QLatin1Char(' ') + tr("%1 failed.").arg(fileCount(errors.size()));
    }
    setStatus(text, !errors.isEmpty());

    // Show the result from the top of the event loop, not from inside the signal delivery.
    QTimer::singleShot(0, this, [this, text, errors, dir] {
        QMessageBox box(errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                        tr("Extract"), text, QMessageBox::Close, this);
        if (!errors.isEmpty()) {
            box.setDetailedText(errors.join(QLatin1Char('\n')));
        }
        QPushButton* show = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
        box.setDefaultButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == show) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    });
}

// ---- Archives panel: scan a folder tree for archives ------------------------------------------

namespace {

constexpr int ArchiveRole = Qt::UserRole + 1;   // index into archives_
constexpr int DataIndexRole = Qt::UserRole + 2; // -1 for the archive row itself
enum ArchiveColumn { ArchiveNameColumn, ArchiveFilesColumn, ArchiveSizeColumn };

} // namespace

QDockWidget* MainWindow::buildArchivesPanel() {
    auto* dock = new QDockWidget(tr("Archives"), this);
    dock->setObjectName(QStringLiteral("archivesDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                          Qt::BottomDockWidgetArea);

    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 4, 4, 4);

    scanButton_ = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                  tr("Scan Folder..."));
    rescanButton_ = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), tr("Rescan"));
    scanCancel_ = new QPushButton(tr("Stop"));
    scanCancel_->hide();
    connect(scanButton_, &QPushButton::clicked, this, &MainWindow::scanFolderDialog);
    connect(rescanButton_, &QPushButton::clicked, this, &MainWindow::rescan);
    connect(scanCancel_, &QPushButton::clicked, this, [this] {
        if (scanner_) {
            scanner_->cancel();
        }
    });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(scanButton_);
    buttons->addWidget(rescanButton_);
    buttons->addWidget(scanCancel_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    scanRoot_ = QSettings().value("scanRoot").toString();
    rescanButton_->setEnabled(!scanRoot_.isEmpty());
    rescanButton_->setToolTip(QDir::toNativeSeparators(scanRoot_));
    scanStatus_ = new QLabel(tr("Scan a folder (a game dump, a drive, ...) to list every "
                                "Driveclub archive inside it."));
    scanStatus_->setWordWrap(true);
    layout->addWidget(scanStatus_);

    archiveTree_ = new QTreeWidget;
    archiveTree_->setColumnCount(3);
    archiveTree_->setHeaderLabels({tr("Archive"), tr("Files"), tr("Size")});
    archiveTree_->setUniformRowHeights(true);
    archiveTree_->setAlternatingRowColors(true);
    archiveTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    archiveTree_->header()->setStretchLastSection(false);
    archiveTree_->header()->setSectionResizeMode(ArchiveNameColumn, QHeaderView::Stretch);
    for (int c = ArchiveFilesColumn; c <= ArchiveSizeColumn; ++c) {
        archiveTree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    connect(archiveTree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item) { openArchiveItem(item); });
    connect(archiveTree_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item) { openArchiveItem(item); });
    connect(archiveTree_, &QWidget::customContextMenuRequested, this, &MainWindow::showArchiveMenu);
    layout->addWidget(archiveTree_, 1);

    dock->setWidget(root);
    dock->setMinimumWidth(320);
    return dock;
}

void MainWindow::scanFolderDialog() {
    const QString start =
        scanRoot_.isEmpty() ? QSettings().value("lastDir", QDir::homePath()).toString() : scanRoot_;
    const QString root =
        QFileDialog::getExistingDirectory(this, tr("Scan folder for Driveclub archives"), start);
    if (!root.isEmpty()) {
        scanFolder(root);
    }
}

void MainWindow::rescan() {
    if (!scanRoot_.isEmpty()) {
        scanFolder(scanRoot_);
    }
}

void MainWindow::scanFolder(const QString& root) {
    if (scanner_) {
        return;
    }
    if (!QFileInfo(root).isDir()) {
        setStatus(tr("%1 is not a folder").arg(QDir::toNativeSeparators(root)), true);
        return;
    }
    scanRoot_ = QFileInfo(root).absoluteFilePath();
    QSettings().setValue("scanRoot", scanRoot_);
    archives_.clear();
    archiveTree_->clear();
    if (archivesDock_->isHidden()) {
        archivesDock_->show();
        resizeDocks({archivesDock_}, {420}, Qt::Horizontal);
    }
    archivesDock_->raise();
    scanStatus_->setToolTip(QString());
    scanStatus_->setStyleSheet(QString());
    scanStatus_->setText(tr("Scanning %1...").arg(QDir::toNativeSeparators(scanRoot_)));
    rescanButton_->setToolTip(QDir::toNativeSeparators(scanRoot_));

    scanner_ = new ArchiveScanner(scanRoot_, this);
    connect(scanner_, &ArchiveScanner::progress, this, [this](int folders, const QString& current) {
        if (!scanner_ || current.isEmpty()) {
            return;
        }
        scanStatus_->setText(tr("Scanning: %1 folders, %2 archives found\n%3")
                                 .arg(number(folders), number(qint64(archives_.size())),
                                      scanStatus_->fontMetrics().elidedText(
                                          QDir::toNativeSeparators(current), Qt::ElideMiddle,
                                          std::max(120, scanStatus_->width()))));
    });
    connect(scanner_, &ArchiveScanner::found, this, &MainWindow::addScannedArchive);
    connect(scanner_, &ArchiveScanner::finished, this, &MainWindow::scanFinished);
    scanCancel_->show();
    updateActions();
    setStatus(tr("Scanning %1...").arg(QDir::toNativeSeparators(scanRoot_)));
    scanner_->start();
}

void MainWindow::addScannedArchive(const std::shared_ptr<ScannedArchive>& archive) {
    const int id = int(archives_.size());
    archives_.push_back(archive);
    const EvoFileSystem& fs = *archive->fs;

    QString name = QDir(scanRoot_).relativeFilePath(archive->folder);
    if (name.isEmpty() || name == QLatin1String(".")) {
        name = QFileInfo(archive->folder).fileName();
    }
    if (fs.isAlpha()) {
        name += tr(" (alpha)");
    }
    auto* item = new QTreeWidgetItem(archiveTree_);
    item->setText(ArchiveNameColumn, QDir::toNativeSeparators(name));
    item->setIcon(ArchiveNameColumn,
                  style()->standardIcon(archive->missing > 0 ? QStyle::SP_MessageBoxWarning
                                                             : QStyle::SP_DriveHDIcon));
    item->setText(ArchiveFilesColumn, number(qint64(fs.index().entries.size())));
    item->setText(ArchiveSizeColumn, formatSize(archive->size));
    QString tip = tr("%1\nVersion %2, built %3 UTC, %4 compression")
                      .arg(QDir::toNativeSeparators(fs.indexPath()))
                      .arg(fs.index().version)
                      .arg(fs.index().timeStamp.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                           compressionName(fs.index().compression));
    if (!fs.isAlpha()) {
        tip += tr("\n%1 data files").arg(archive->dataFiles.size());
    }
    if (archive->missing > 0) {
        tip += tr(", %1 missing or unreadable").arg(archive->missing);
        item->setForeground(ArchiveNameColumn, QColor(0xc0, 0x20, 0x20));
    }
    item->setToolTip(ArchiveNameColumn, tip);
    item->setTextAlignment(ArchiveFilesColumn, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(ArchiveSizeColumn, Qt::AlignRight | Qt::AlignVCenter);
    item->setData(ArchiveNameColumn, ArchiveRole, id);
    item->setData(ArchiveNameColumn, DataIndexRole, -1);

    if (!fs.isAlpha()) {
        for (const ScannedArchive::DataFile& data : archive->dataFiles) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(ArchiveNameColumn, data.name);
            child->setIcon(ArchiveNameColumn,
                           style()->standardIcon(data.present ? QStyle::SP_FileIcon
                                                              : QStyle::SP_MessageBoxWarning));
            child->setText(ArchiveFilesColumn, number(data.files));
            child->setText(ArchiveSizeColumn, formatSize(data.size));
            if (data.present) {
                child->setToolTip(
                    ArchiveNameColumn,
                    QDir::toNativeSeparators(QDir(archive->folder).filePath(data.name)));
            } else {
                child->setText(ArchiveNameColumn, tr("%1 (%2)").arg(data.name, data.error));
                child->setForeground(ArchiveNameColumn, QColor(0xc0, 0x20, 0x20));
                child->setToolTip(ArchiveNameColumn,
                                  tr("Its file names are listed from game.ndx, but the files "
                                     "cannot be previewed or extracted."));
            }
            child->setTextAlignment(ArchiveFilesColumn, Qt::AlignRight | Qt::AlignVCenter);
            child->setTextAlignment(ArchiveSizeColumn, Qt::AlignRight | Qt::AlignVCenter);
            child->setData(ArchiveNameColumn, ArchiveRole, id);
            child->setData(ArchiveNameColumn, DataIndexRole, data.index);
        }
    }
}

void MainWindow::scanFinished(int archives, int orphans, const QStringList& failed,
                              bool cancelled) {
    scanner_->deleteLater();
    scanner_ = nullptr;
    scanCancel_->hide();
    updateActions();

    QString text = cancelled ? tr("Stopped. ") : QString();
    text += archives == 0
                ? tr("No Driveclub archives found in %1.").arg(QDir::toNativeSeparators(scanRoot_))
            : archives == 1 ? tr("1 archive found. Click it to browse.")
                            : tr("%1 archives found. Click one to browse.").arg(number(archives));
    if (orphans > 0) {
        text += QLatin1Char(' ') +
                (orphans == 1 ? tr("1 data file was skipped because there is no game.ndx next "
                                   "to it (it holds the file names).")
                              : tr("%1 data files were skipped because there is no game.ndx "
                                   "next to them (it holds the file names).")
                                    .arg(number(orphans)));
    }
    if (!failed.isEmpty()) {
        text += QLatin1Char(' ') + tr("%1 could not be read (hover here for details).")
                                       .arg(fileCount(qint64(failed.size())));
        scanStatus_->setToolTip(failed.join(QLatin1Char('\n')));
    }
    scanStatus_->setText(text);
    setStatus(text, archives == 0 && !cancelled);

    if (archives_.size() == 1 && !fs_) {
        archiveTree_->setCurrentItem(archiveTree_->topLevelItem(0));
        openArchiveItem(archiveTree_->topLevelItem(0));
    }
    if (archives_.size() <= 3) {
        archiveTree_->expandAll();
    }
}

std::shared_ptr<ScannedArchive> MainWindow::archiveFor(QTreeWidgetItem* item) const {
    if (!item) {
        return {};
    }
    const int id = item->data(ArchiveNameColumn, ArchiveRole).toInt();
    if (id < 0 || id >= int(archives_.size())) {
        return {};
    }
    return archives_[size_t(id)];
}

void MainWindow::openArchiveItem(QTreeWidgetItem* item) {
    const auto archive = archiveFor(item);
    if (!archive || busy()) {
        return;
    }
    const int dataIndex = item->data(ArchiveNameColumn, DataIndexRole).toInt();
    if (fs_ == archive->fs) {
        // Already showing this archive: just switch the data file.
        const int at = dataFilter_->findData(dataIndex);
        if (at >= 0 && at != dataFilter_->currentIndex()) {
            dataFilter_->setCurrentIndex(at);
        }
        return;
    }
    showFileSystem(archive->fs, archive->fs->indexPath(), dataIndex);
}

void MainWindow::extractArchiveItem(QTreeWidgetItem* item) {
    const auto archive = archiveFor(item);
    if (!archive) {
        return;
    }
    const int dataIndex = item->data(ArchiveNameColumn, DataIndexRole).toInt();
    const auto& entries = archive->fs->index().entries;
    std::vector<int> selected;
    for (int i = 0; i < int(entries.size()); ++i) {
        if (dataIndex < 0 || entries[size_t(i)].datIndex == dataIndex) {
            selected.push_back(i);
        }
    }
    const QString what = dataIndex < 0 ? tr("all of %1").arg(item->text(ArchiveNameColumn))
                                       : archive->fs->dataFileName(dataIndex);
    startExtraction(archive->fs, std::move(selected), what);
}

void MainWindow::showArchiveMenu(const QPoint& pos) {
    QTreeWidgetItem* item = archiveTree_->itemAt(pos);
    QMenu menu(this);
    if (const auto archive = archiveFor(item)) {
        const int dataIndex = item->data(ArchiveNameColumn, DataIndexRole).toInt();
        menu.addAction(tr("Browse"), this, [this, item] { openArchiveItem(item); });
        QAction* extract = menu.addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                          dataIndex < 0 ? tr("Extract Archive...")
                                                        : tr("Extract %1...").arg(item->text(0)),
                                          this, [this, item] { extractArchiveItem(item); });
        extract->setEnabled(!busy());
        QAction* textures =
            menu.addAction(tr("Export Textures..."), this, [this, archive, dataIndex, item] {
                std::vector<int> entries;
                const auto& all = archive->fs->index().entries;
                for (int i = 0; i < int(all.size()); ++i) {
                    if (dataIndex < 0 || all[size_t(i)].datIndex == dataIndex) {
                        entries.push_back(i);
                    }
                }
                exportTextures(archive->fs, std::move(entries), item->text(0));
            });
        textures->setEnabled(!busy());
        const auto fs = archive->fs;
        if (dataIndex >= 0) {
            menu.addAction(tr("Export File Names of %1...").arg(fs->dataFileName(dataIndex)), this,
                           [this, fs, dataIndex] {
                               exportFileNames(
                                   fs, entriesInDataFile(*fs, dataIndex),
                                   fs->dataFileName(dataIndex),
                                   QFileInfo(fs->dataFileName(dataIndex)).completeBaseName());
                           });
        } else {
            menu.addAction(tr("Export File Names..."), this, [this, fs] {
                exportFileNames(fs, entriesInDataFile(*fs, -1), tr("all data files"),
                                QStringLiteral("files"));
            });
            if (!fs->isAlpha()) {
                menu.addAction(tr("Export Names per Data File..."), this,
                               [this, fs] { exportListsPerDataFile(fs); });
            }
        }
        const QString folder = archive->folder;
        menu.addAction(tr("Open Containing Folder"), this,
                       [folder] { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        menu.addSeparator();
    }
    menu.addAction(scanFolder_);
    QAction* again = menu.addAction(tr("Rescan"), this, &MainWindow::rescan);
    again->setEnabled(!scanner_ && !scanRoot_.isEmpty());
    menu.exec(archiveTree_->viewport()->mapToGlobal(pos));
}

void MainWindow::updateActions() {
    const bool open = fs_ != nullptr;
    const bool idle = !busy();
    const bool selection = open && tree_->selectionModel()->hasSelection();
    openFile_->setEnabled(idle);
    openFolder_->setEnabled(idle);
    recentMenu_->setEnabled(idle && !recentMenu_->isEmpty());
    extractSelected_->setEnabled(idle && selection);
    extractVisible_->setEnabled(idle && open && proxy_->rowCount() > 0);
    extractAll_->setEnabled(idle && open);
    exportList_->setEnabled(open && proxy_->rowCount() > 0);
    exportPerData_->setEnabled(open && !fs_->isAlpha());
    exportTextures_->setEnabled(idle && open && proxy_->rowCount() > 0);
    galleryAction_->setEnabled(open && proxy_->rowCount() > 0);
    const bool scanning = scanner_ != nullptr;
    scanFolder_->setEnabled(!scanning);
    scanButton_->setEnabled(!scanning);
    rescanButton_->setEnabled(!scanning && !scanRoot_.isEmpty());
}

void MainWindow::setStatus(const QString& text, bool error) {
    statusBar()->setStyleSheet(error ? QStringLiteral("color: #c02020;") : QString());
    statusBar()->showMessage(text);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() &&
        !event->mimeData()->urls().first().toLocalFile().isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        openPath(urls.first().toLocalFile());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (textureJob_) {
        if (QMessageBox::question(this, tr("Quit"),
                                  tr("A texture export is running. Cancel it and quit?")) !=
            QMessageBox::Yes) {
            event->ignore();
            return;
        }
        textureJob_->cancel();
        delete textureJob_; // waits for the worker to stop
        textureJob_ = nullptr;
    }
    if (job_) {
        if (QMessageBox::question(this, tr("Quit"),
                                  tr("An extraction is running. Cancel it and quit?")) !=
            QMessageBox::Yes) {
            event->ignore();
            return;
        }
        job_->cancel();
        delete job_; // waits for the worker to stop
        job_ = nullptr;
    }
    if (scanner_) {
        scanner_->cancel();
        delete scanner_; // waits for the worker to stop
        scanner_ = nullptr;
    }
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    if (auto* splitter = qobject_cast<QSplitter*>(centralWidget())) {
        settings.setValue("splitter", splitter->saveState());
    }
    QMainWindow::closeEvent(event);
}

bool MainWindow::busy() const {
    return job_ != nullptr || textureJob_ != nullptr;
}

QProgressDialog* MainWindow::makeProgress(const QString& title) {
    auto* dialog = new QProgressDialog(tr("Preparing..."), tr("Cancel"), 0, 1000, this);
    dialog->setWindowTitle(title);
    // Not modal: a modal QProgressDialog runs the event loop inside setValue(), which would let
    // a job's finished signal be handled in the middle of a progress update. The work runs on
    // a worker thread, so the window stays responsive anyway; updateActions() disables
    // everything that must not run meanwhile.
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumDuration(0);
    dialog->setMinimumWidth(520);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    return dialog;
}

void MainWindow::showPreviewImage(const QImage& image) {
    previewImage_ = image;
    previewTabs_->setTabEnabled(ImageTab, true);
    updatePreviewImage();
}

void MainWindow::updatePreviewImage() {
    if (previewImage_.isNull()) {
        imageView_->clear();
        return;
    }
    QPixmap pixmap = QPixmap::fromImage(previewImage_);
    const QSize area = imageArea_->viewport()->size() - QSize(4, 4);
    if (pixmap.width() > area.width() || pixmap.height() > area.height()) {
        pixmap = pixmap.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else if (pixmap.width() * 2 <= area.width() && pixmap.height() * 2 <= area.height()) {
        // Small images: enlarge by whole steps so pixels stay sharp.
        const int factor = std::min(area.width() / pixmap.width(), area.height() / pixmap.height());
        pixmap = pixmap.scaled(pixmap.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    imageView_->setPixmap(pixmap);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == imageArea_->viewport() && event->type() == QEvent::Resize &&
        !previewImage_.isNull()) {
        updatePreviewImage();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::textureReady(int ticket, const QImage& image, const QString& info,
                              const QString& error) {
    if (ticket != previewTicket_) {
        return; // the selection moved on
    }
    previewTicket_ = -1;
    QString note = previewBaseNote_;
    if (image.isNull()) {
        note += error.isEmpty() ? tr(" No textures in this pack.") : tr(" Texture: %1").arg(error);
        previewNote_->setText(note);
        return;
    }
    previewNote_->setText(note + QLatin1Char(' ') + info + QLatin1Char('.'));
    showPreviewImage(image);
    previewTabs_->setCurrentIndex(ImageTab);
}

std::vector<int> MainWindow::selectionOrVisible() const {
    return tree_->selectionModel()->hasSelection() ? selectedEntries() : visibleEntries();
}

void MainWindow::exportTextures(const std::shared_ptr<const EvoFileSystem>& fs,
                                std::vector<int> entries, const QString& what) {
    if (!fs || busy()) {
        return;
    }
    std::vector<int> packs = TextureBatch::packsIn(*fs, entries);
    if (packs.empty()) {
        QMessageBox::information(this, tr("Export textures"),
                                 tr("There are no resource packs (.rpk) in %1.").arg(what));
        return;
    }

    QSettings settings;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export textures"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        tr("Export the textures of %1 resource packs in %2.<br>Single-texture packs are written "
           "where the pack was (<i>x.tif_pixel_buffer.rpk</i> becomes <i>x.dds</i>); other packs "
           "get a folder of their own.")
            .arg(number(qint64(packs.size())), what.toHtmlEscaped()));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* dds = new QRadioButton(tr("DDS (the original texture data)"));
    auto* png = new QRadioButton(tr("PNG (decoded, viewable anywhere)"));
    auto* both = new QRadioButton(tr("DDS and PNG"));
    const int format = settings.value("textures/format", 0).toInt();
    (format == 1 ? png : format == 2 ? both : dds)->setChecked(true);
    layout->addWidget(dds);
    layout->addWidget(png);
    layout->addWidget(both);
    auto* everything =
        new QCheckBox(tr("Also export XML, BIN and other resources (each pack in its own folder)"));
    everything->setChecked(settings.value("textures/everything", false).toBool());
    layout->addWidget(everything);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Choose Folder..."));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    settings.setValue("textures/format", png->isChecked() ? 1 : both->isChecked() ? 2 : 0);
    settings.setValue("textures/everything", everything->isChecked());

    const QString start =
        settings.value("textures/dir", QDir(fs->directory()).filePath(QStringLiteral("textures")))
            .toString();
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Export textures of %1 packs to").arg(number(qint64(packs.size()))), start);
    if (folder.isEmpty()) {
        return;
    }
    settings.setValue("textures/dir", folder);

    ResourceExportOptions options;
    options.dds = !png->isChecked();
    options.png = !dds->isChecked();
    options.texturesOnly = !everything->isChecked();
    options.other = everything->isChecked();

    textureJob_ = new TextureExportJob(fs, std::move(packs), folder, options, this);
    progress_ = makeProgress(tr("Exporting textures"));
    connect(progress_, &QProgressDialog::canceled, textureJob_, &TextureExportJob::cancel);
    connect(textureJob_, &TextureExportJob::progress, this,
            [this](int done, int total, int textures, const QString& current) {
                const QPointer<QProgressDialog> dialog = progress_;
                if (!dialog) {
                    return;
                }
                dialog->setLabelText(
                    tr("%1 of %2 packs, %3 textures written\n%4")
                        .arg(number(done), number(total), number(textures),
                             dialog->fontMetrics().elidedText(current, Qt::ElideMiddle, 480)));
                dialog->setValue(total > 0 ? int(qint64(done) * 1000 / total) : 0);
            });
    connect(textureJob_, &TextureExportJob::finished, this, &MainWindow::textureExportFinished);
    updateActions();
    setStatus(tr("Exporting textures to %1...").arg(QDir::toNativeSeparators(folder)));
    textureJob_->start();
}

void MainWindow::textureExportFinished(const TextureBatchStats& stats, const QStringList& errors,
                                       bool cancelled) {
    const QString dir = textureJob_->outputDir();
    textureJob_->deleteLater();
    textureJob_ = nullptr;
    if (progress_) {
        progress_->hide();
        progress_->deleteLater();
        progress_ = nullptr;
    }
    updateActions();

    QString text = cancelled ? tr("Cancelled. ") : QString();
    text += tr("Wrote %1 textures from %2 packs.").arg(number(stats.textures), number(stats.packs));
    if (stats.resources > 0) {
        text += QLatin1Char(' ') + tr("Also %1 other resources.").arg(number(stats.resources));
    }
    if (stats.noTextures > 0) {
        text += QLatin1Char(' ') + tr("%1 packs had no textures.").arg(number(stats.noTextures));
    }
    if (!errors.isEmpty()) {
        text += QLatin1Char(' ') + tr("%1 problems.").arg(number(qint64(errors.size())));
    }
    setStatus(text, !errors.isEmpty());
    QTimer::singleShot(0, this, [this, text, errors, dir] {
        QMessageBox box(errors.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
                        tr("Export textures"), text, QMessageBox::Close, this);
        if (!errors.isEmpty()) {
            box.setDetailedText(errors.join(QLatin1Char('\n')));
        }
        QPushButton* show = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
        box.setDefaultButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == show) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        }
    });
}

QString MainWindow::showTable(const QString& text) {
    const CsvTable table = parseCsv(text, MaxTableRows + 1);
    tableFilter_->blockSignals(true);
    tableFilter_->clear();
    tableFilter_->blockSignals(false);
    tableView_->setSortingEnabled(false);
    tableView_->clear();
    if (table.rows.empty()) {
        tableView_->setRowCount(0);
        tableView_->setColumnCount(0);
        tableInfo_->setText(tr("empty"));
        return tr("Empty table.");
    }
    int columns = 0;
    for (const QStringList& row : table.rows) {
        columns = std::max(columns, int(row.size()));
    }
    // The first row is the header.
    QStringList header = table.rows.front();
    while (header.size() < columns) {
        header << QString::number(header.size() + 1);
    }
    const int rows = int(table.rows.size()) - 1;
    tableView_->setColumnCount(columns);
    tableView_->setHorizontalHeaderLabels(header);
    tableView_->setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        const QStringList& row = table.rows[size_t(r) + 1];
        for (int c = 0; c < row.size(); ++c) {
            tableView_->setItem(r, c, new QTableWidgetItem(row.at(c)));
        }
    }
    tableView_->resizeColumnsToContents();
    for (int c = 0; c < columns; ++c) {
        tableView_->setColumnWidth(c, std::min(tableView_->columnWidth(c), 360));
    }
    // Keep file order until a header is clicked.
    tableView_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    tableView_->setSortingEnabled(true);
    filterTable();

    const QString delimiter = table.delimiter == QLatin1Char('\t')
                                  ? tr("tabs")
                                  : QStringLiteral("\"%1\"").arg(table.delimiter);
    QString info = tr("Table: %1 rows x %2 columns, separated by %3")
                       .arg(number(rows), number(columns), delimiter);
    if (table.truncated) {
        info += tr(" (first %1 rows)").arg(number(MaxTableRows));
    }
    return info;
}

void MainWindow::filterTable() {
    const QString text = tableFilter_->text().trimmed();
    int shown = 0;
    for (int r = 0; r < tableView_->rowCount(); ++r) {
        bool match = text.isEmpty();
        for (int c = 0; !match && c < tableView_->columnCount(); ++c) {
            const QTableWidgetItem* item = tableView_->item(r, c);
            match = item && item->text().contains(text, Qt::CaseInsensitive);
        }
        tableView_->setRowHidden(r, !match);
        shown += match ? 1 : 0;
    }
    tableInfo_->setText(
        text.isEmpty() ? tr("%1 rows").arg(number(tableView_->rowCount()))
                       : tr("%1 of %2 rows").arg(number(shown), number(tableView_->rowCount())));
}

void MainWindow::copyTableCells() {
    const QList<QTableWidgetSelectionRange> ranges = tableView_->selectedRanges();
    if (ranges.isEmpty()) {
        return;
    }
    // Tab-separated, so it pastes straight into a spreadsheet.
    QStringList lines;
    for (const QTableWidgetSelectionRange& range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
            if (tableView_->isRowHidden(r)) {
                continue;
            }
            QStringList cells;
            for (int c = range.leftColumn(); c <= range.rightColumn(); ++c) {
                const QTableWidgetItem* item = tableView_->item(r, c);
                cells << (item ? item->text() : QString());
            }
            lines << cells.join(QLatin1Char('\t'));
        }
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}
