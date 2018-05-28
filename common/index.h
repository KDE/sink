#pragma once

#include "sink_export.h"
#include <string>
#include <functional>
#include <QString>
#include "storage.h"
#include "log.h"

/**
 * An index for value pairs.
 */
class SINK_EXPORT Index
{
public:
    enum ErrorCodes
    {
        IndexNotAvailable = -1
    };

    class Error
    {
    public:
        Error(const QByteArray &s, int c, const QByteArray &m) : store(s), message(m), code(c)
        {
        }
        QByteArray store;
        QByteArray message;
        int code;
    };

    Index(const QString &storageRoot, const QString &name, Sink::Storage::DataStore::AccessMode mode = Sink::Storage::DataStore::ReadOnly);
    Index(const QString &storageRoot, const Sink::Storage::DbLayout &layout, Sink::Storage::DataStore::AccessMode mode = Sink::Storage::DataStore::ReadOnly);
    Index(const QByteArray &name, Sink::Storage::DataStore::Transaction &);

    void add(const QByteArray &key, const QByteArray &value);
    void remove(const QByteArray &key, const QByteArray &value);

    void lookup(const QByteArray &key, const std::function<void(const QByteArray &value)> &resultHandler, const std::function<void(const Error &error)> &errorHandler,
        bool matchSubStringKeys = false);
    QByteArray lookup(const QByteArray &key);

    void rangeLookup(const QByteArray &lowerBound, const QByteArray &upperBound,
        const std::function<void(const QByteArray &value)> &resultHandler,
        const std::function<void(const Error &error)> &errorHandler);

private:
    Q_DISABLE_COPY(Index);
    Sink::Storage::DataStore::Transaction mTransaction;
    Sink::Storage::DataStore::NamedDatabase mDb;
    QString mName;
    Sink::Log::Context mLogCtx;
};
