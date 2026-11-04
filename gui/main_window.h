// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "archive_scanner.h"
#include "evo_file_system.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <vector>

class EvoReader;
class QDockWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class ExtractJob;
class TexturePreviewer;
class TextureExportJob;
struct TextureBatchStats;
class FileTreeModel;
class FileTreeProxy;
class QAction;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QProgressDialog;
class QScrollArea;
class QTabWidget;
class QTableWidget;
class QTimer;
class QTreeView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    bool openPath(const QString& path);
    void scanFolder(const QString& root);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildDetails();
    QDockWidget* buildArchivesPanel();
    void showFileSystem(std::shared_ptr<const EvoFileSystem> fs, const QString& path,
                        int dataIndex);
    void openFileDialog();
    void openFolderDialog();
    void updateRecentMenu();
    void fillDataFilter(int dataIndex);
    void applyDataFilter();
    void applyTextFilter();
    void showSelection();
    void showPreview(int entryIndex);
    // Fills the Table tab from CSV text; returns a short description for the note.
    QString showTable(const QString& text);
    void filterTable();
    void copyTableCells();
    void showContextMenu(const QPoint& pos);
    // Opens the .rpk at entries[entryIndex] (or a .rpk on disk) in its own window.
    void openResourcePack(int entryIndex);
    void openResourcePack(const std::shared_ptr<const EvoFileSystem>& fs, int entryIndex);
    // A thumbnail gallery of the textures in the selection (or everything shown).
    void showGallery();
    bool openPackFile(const QString& path);
    int currentPackEntry() const;
    void copySelectedPaths();
    void exportFileList();
    // Asks where and in which format, then writes the names of `entries`.
    void exportFileNames(const std::shared_ptr<const EvoFileSystem>& fs, std::vector<int> entries,
                         const QString& what, const QString& defaultName);
    // Asks for a folder and writes one list per data file into it.
    void exportListsPerDataFile(const std::shared_ptr<const EvoFileSystem>& fs);

    // Entries under the selected rows (folders expand to the files the filter leaves visible).
    std::vector<int> selectedEntries() const;
    std::vector<int> visibleEntries() const;
    void collectEntries(const QModelIndex& proxyIndex, std::vector<int>* out) const;
    void extractSelected();
    void extractVisible();
    void extractAll();
    void startExtraction(std::shared_ptr<const EvoFileSystem> fs, std::vector<int> entries,
                         const QString& what);

    // Archives panel (folder scan).
    void scanFolderDialog();
    void rescan();
    void addScannedArchive(const std::shared_ptr<ScannedArchive>& archive);
    void scanFinished(int archives, int orphans, const QStringList& failed, bool cancelled);
    void openArchiveItem(QTreeWidgetItem* item);
    void showArchiveMenu(const QPoint& pos);
    void extractArchiveItem(QTreeWidgetItem* item);
    std::shared_ptr<ScannedArchive> archiveFor(QTreeWidgetItem* item) const;
    void extractionFinished(int extracted, const QStringList& errors, bool cancelled);

    // Textures: inline preview of a pack's first texture, and batch export from many packs.
    void showPreviewImage(const QImage& image);
    void updatePreviewImage();
    void textureReady(int ticket, const QImage& image, const QString& info, const QString& error);
    // The selection, or everything shown when nothing is selected.
    std::vector<int> selectionOrVisible() const;
    void exportTextures(const std::shared_ptr<const EvoFileSystem>& fs, std::vector<int> entries,
                        const QString& what);
    void textureExportFinished(const TextureBatchStats& stats, const QStringList& errors,
                               bool cancelled);
    QProgressDialog* makeProgress(const QString& title);
    bool busy() const;

    void updateActions();
    void setStatus(const QString& text, bool error = false);

    std::shared_ptr<const EvoFileSystem> fs_;
    std::unique_ptr<EvoReader> previewReader_;
    FileTreeModel* model_;
    FileTreeProxy* proxy_;
    ExtractJob* job_ = nullptr;
    TextureExportJob* textureJob_ = nullptr;
    TexturePreviewer* previewer_ = nullptr;
    int previewTicket_ = -1;
    QString previewBaseNote_;
    QImage previewImage_;
    QProgressDialog* progress_ = nullptr;
    ArchiveScanner* scanner_ = nullptr;
    std::vector<std::shared_ptr<ScannedArchive>> archives_;
    QString scanRoot_;

    QAction* openFile_;
    QAction* openFolder_;
    QAction* extractSelected_;
    QAction* extractVisible_;
    QAction* extractAll_;
    QAction* exportList_;
    QAction* exportPerData_;
    QAction* exportTextures_;
    QAction* scanFolder_;
    QMenu* recentMenu_;
    QComboBox* dataFilter_;
    QLineEdit* filter_;
    QTimer* filterTimer_;
    QCheckBox* verify_;
    QLabel* summary_;

    QDockWidget* archivesDock_;
    QTreeWidget* archiveTree_;
    QLabel* scanStatus_;
    QPushButton* scanButton_;
    QPushButton* rescanButton_;
    QPushButton* scanCancel_;

    QTreeView* tree_;
    QFormLayout* detailsForm_;
    QLabel* detailPath_;
    QLabel* detailSize_;
    QLabel* detailData_;
    QLabel* detailOffset_;
    QLabel* detailChunks_;
    QLabel* detailHash_;
    QLabel* detailMd5_;
    QTabWidget* previewTabs_;
    QPlainTextEdit* hexView_;
    QPlainTextEdit* textView_;
    QWidget* tableTab_;
    QLineEdit* tableFilter_;
    QTableWidget* tableView_;
    QLabel* tableInfo_;
    QScrollArea* imageArea_;
    QLabel* imageView_;
    QLabel* previewNote_;
    QPushButton* openPack_;
    QPushButton* showGallery_;
    QAction* galleryAction_;
};
