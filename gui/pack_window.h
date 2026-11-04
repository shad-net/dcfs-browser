// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "resource_export.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QTableWidget;
class QTextBrowser;

class PackWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PackWindow(std::unique_ptr<PackContext> context, QWidget* parent = nullptr);
    ~PackWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void fillTable();
    void applyFilter();
    void showSelection();
    void showTexture(const ResourceInfo& info);
    void showMaterial(const ResourceInfo& info);
    void selectResource(quint64 uid);
    void updateImage();
    std::vector<int> selectedResources() const;
    std::vector<int> visibleResources() const;
    void exportResources(std::vector<int> resources, const QString& what);

    std::unique_ptr<PackContext> context_;
    QImage image_; // decoded texture, before the channel view is applied

    QLabel* summary_;
    QComboBox* typeFilter_;
    QLineEdit* nameFilter_;
    QCheckBox* png_;
    QCheckBox* other_;
    QPushButton* exportSelected_;
    QPushButton* exportVisible_;
    QTableWidget* table_;

    QLabel* details_;
    QTabWidget* tabs_;
    QWidget* imageTab_;
    QComboBox* channels_;
    QCheckBox* fit_;
    QScrollArea* imageArea_;
    QLabel* imageView_;
    QPlainTextEdit* text_;
    QPlainTextEdit* hex_;
    QTextBrowser* material_;
};
