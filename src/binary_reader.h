// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QtEndian>

#include <cstring>

class BinaryReader {
public:
    explicit BinaryReader(QIODevice& device) : device_(device) {}

    bool ok() const {
        return ok_;
    }
    qint64 pos() const {
        return device_.pos();
    }
    bool seek(qint64 pos) {
        ok_ = ok_ && device_.seek(pos);
        return ok_;
    }

    template <typename T>
    T read() {
        T value{};
        if (!ok_) {
            return value;
        }
        char buffer[sizeof(T)];
        if (device_.read(buffer, sizeof(T)) != qint64(sizeof(T))) {
            ok_ = false;
            return value;
        }
        std::memcpy(&value, buffer, sizeof(T));
        return qFromLittleEndian(value);
    }

    QByteArray bytes(qint64 count) {
        if (!ok_ || count < 0) {
            ok_ = false;
            return {};
        }
        QByteArray data = device_.read(count);
        if (data.size() != count) {
            ok_ = false;
            return {};
        }
        return data;
    }

private:
    QIODevice& device_;
    bool ok_ = true;
};
