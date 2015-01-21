#pragma once

#include <string>
#include <functional>
#include <QString>
#include "storage.h"

/**
 * An index for value pairs.
 */
class Index
{
public:
    class Error
    {
    public:
        Error(const std::string &s, int c, const std::string &m)
            : store(s), message(m), code(c) {}
        std::string store;
        std::string message;
        int code;
    };

    Index(const QString &storageRoot, const QString &name, Akonadi2::Storage::AccessMode mode = Akonadi2::Storage::ReadOnly);

    void add(const QByteArray &key, const QByteArray &value);
    // void remove(const QByteArray &key, const QByteArray &value);

    void lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                       const std::function<void(const Error &error)> &errorHandler);

private:
    Q_DISABLE_COPY(Index);
    Akonadi2::Storage mStorage;
};
