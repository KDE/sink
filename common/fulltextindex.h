#pragma once

#include "sink_export.h"

#include <string>
#include <functional>
#include <QString>
#include <memory>
#include "storage.h"
#include "log.h"

namespace Xapian {
    class Database;
    class WritableDatabase;
};

/**
 * An index for value pairs.
 */
class SINK_EXPORT FulltextIndex
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

    /* FulltextIndex(const QString &storageRoot, const QString &name, Sink::Storage::AccessMode mode = Sink::Storage::ReadOnly); */
    /* FulltextIndex(const QByteArray &name); */
    FulltextIndex(const QByteArray &resourceInstanceIdentifier, Sink::Storage::DataStore::AccessMode mode = Sink::Storage::DataStore::ReadOnly);
    ~FulltextIndex();

    void add(const QByteArray &key, const QString &value);
    void add(const QByteArray &key, const QList<QPair<QString, QString>> &values);
    void remove(const QByteArray &key);

    /* void startTransaction(); */
    /* void commit(qint64 revision); */
    void commitTransaction();
    void abortTransaction();

    QVector<QByteArray> lookup(const QString &key);

private:
    Xapian::WritableDatabase* writableDatabase();
    Q_DISABLE_COPY(FulltextIndex);
    Xapian::Database *mDb{nullptr};
    QString mName;
    QString mDbPath;
    bool mHasTransactionOpen{false};
};
