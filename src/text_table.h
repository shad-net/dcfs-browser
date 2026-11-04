// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Text decoding (UTF-8 / UTF-16, with or without a byte order mark) and CSV parsing for the
// preview.

#pragma once

#include <QByteArray>
#include <QChar>
#include <QString>
#include <QStringList>

#include <vector>

// True when `data` starts with a BOM, or looks like UTF-16 text (many zero bytes in one of
// the two byte positions).
bool looksLikeUnicodeText(const QByteArray& data);

// Decodes by BOM, then by the UTF-16 heuristic, else as UTF-8. *encoding gets a short name
// ("UTF-8", "UTF-16LE", ...).
QString decodeText(const QByteArray& data, QString* encoding = nullptr);

struct CsvTable {
    QChar delimiter;
    std::vector<QStringList> rows; // the first row is usually the header
    bool truncated = false;        // stopped at maxRows
};

// RFC 4180 style: quoted fields may hold delimiters, quotes ("") and line breaks. The delimiter
// is whichever of , ; tab | is most common in the first line.
CsvTable parseCsv(const QString& text, int maxRows);
