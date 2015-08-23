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
    enum ErrorCodes {
        IndexNotAvailable = -1
    };

    class Error
    {
    public:
        Error(const QByteArray &s, int c, const QByteArray &m)
            : store(s), message(m), code(c) {}
        QByteArray store;
        QByteArray message;
        int code;
    };

    Index(const QString &storageRoot, const QString &name, Akonadi2::Storage::AccessMode mode = Akonadi2::Storage::ReadOnly);
    Index(const QByteArray &name, Akonadi2::Storage::Transaction &);

    void add(const QByteArray &key, const QByteArray &value);
    // void remove(const QByteArray &key, const QByteArray &value);

    void lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                       const std::function<void(const Error &error)> &errorHandler);

private:
    Q_DISABLE_COPY(Index);
    Akonadi2::Storage::Transaction mTransaction;
    Akonadi2::Storage::NamedDatabase mDb;
};
