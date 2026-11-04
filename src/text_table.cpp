// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "text_table.h"

#include <QStringDecoder>

#include <algorithm>

namespace {

enum class Encoding { Utf8, Utf8Bom, Utf16Le, Utf16Be };

Encoding detect(const QByteArray& data) {
    if (data.startsWith("\xEF\xBB\xBF")) {
        return Encoding::Utf8Bom;
    }
    if (data.startsWith("\xFF\xFE")) {
        return Encoding::Utf16Le;
    }
    if (data.startsWith("\xFE\xFF")) {
        return Encoding::Utf16Be;
    }
    // No BOM: ASCII-range UTF-16 has a zero in every other byte.
    const qsizetype sample = std::min<qsizetype>(data.size(), 4096) & ~qsizetype(1);
    if (sample >= 4) {
        int evenZero = 0;
        int oddZero = 0;
        for (qsizetype i = 0; i < sample; i += 2) {
            evenZero += data.at(i) == 0 ? 1 : 0;
            oddZero += data.at(i + 1) == 0 ? 1 : 0;
        }
        const int pairs = int(sample / 2);
        if (oddZero * 10 >= pairs * 6 && evenZero * 10 < pairs) {
            return Encoding::Utf16Le;
        }
        if (evenZero * 10 >= pairs * 6 && oddZero * 10 < pairs) {
            return Encoding::Utf16Be;
        }
    }
    return Encoding::Utf8;
}

} // namespace

bool looksLikeUnicodeText(const QByteArray& data) {
    return detect(data) != Encoding::Utf8;
}

QString decodeText(const QByteArray& data, QString* encoding) {
    switch (detect(data)) {
    case Encoding::Utf8Bom:
        if (encoding) {
            *encoding = QStringLiteral("UTF-8 (BOM)");
        }
        return QString::fromUtf8(data.mid(3));
    case Encoding::Utf16Le: {
        if (encoding) {
            *encoding = QStringLiteral("UTF-16LE");
        }
        QStringDecoder decoder(QStringDecoder::Utf16LE, QStringDecoder::Flag::Stateless);
        return decoder.decode(data.startsWith("\xFF\xFE") ? data.mid(2) : data);
    }
    case Encoding::Utf16Be: {
        if (encoding) {
            *encoding = QStringLiteral("UTF-16BE");
        }
        QStringDecoder decoder(QStringDecoder::Utf16BE, QStringDecoder::Flag::Stateless);
        return decoder.decode(data.startsWith("\xFE\xFF") ? data.mid(2) : data);
    }
    case Encoding::Utf8:
        break;
    }
    if (encoding) {
        *encoding = QStringLiteral("UTF-8");
    }
    return QString::fromUtf8(data);
}

CsvTable parseCsv(const QString& text, int maxRows) {
    CsvTable table;

    // Delimiter: the most common candidate in the first line, outside quotes.
    const QChar candidates[] = {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'),
                                QLatin1Char('|')};
    int counts[4] = {0, 0, 0, 0};
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            break;
        } else if (!quoted) {
            for (int i = 0; i < 4; ++i) {
                counts[i] += c == candidates[i] ? 1 : 0;
            }
        }
    }
    const int best = int(std::max_element(counts, counts + 4) - counts);
    table.delimiter = candidates[best];

    QStringList row;
    QString field;
    quoted = false;
    bool fieldStarted = false;
    auto endRow = [&]() {
        row << field;
        field.clear();
        fieldStarted = false;
        // Skip blank lines.
        if (!(row.size() == 1 && row.first().isEmpty())) {
            table.rows.push_back(row);
        }
        row.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (c == table.delimiter) {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() &&
                text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            endRow();
            if (int(table.rows.size()) >= maxRows) {
                table.truncated = i + 1 < text.size();
                return table;
            }
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        endRow();
    }
    return table;
}
