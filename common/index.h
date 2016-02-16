#pragma once

#include "sink_export.h"
#include <string>
#include <functional>
#include <QString>
#include "storage.h"

/**
 * An index for value pairs.
 */
class SINK_EXPORT Index
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

    Index(const QString &storageRoot, const QString &name, Sink::Storage::AccessMode mode = Sink::Storage::ReadOnly);
    Index(const QByteArray &name, Sink::Storage::Transaction &);

    void add(const QByteArray &key, const QByteArray &value);
    void remove(const QByteArray &key, const QByteArray &value);

    void lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler,
                                       const std::function<void(const Error &error)> &errorHandler, bool matchSubStringKeys = false);
    QByteArray lookup(const QByteArray &key);

private:
    Q_DISABLE_COPY(Index);
    Sink::Storage::Transaction mTransaction;
    Sink::Storage::NamedDatabase mDb;
    QString mName;
};
