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

class SINK_EXPORT FulltextIndex
{
public:
    FulltextIndex(const QByteArray &resourceInstanceIdentifier, Sink::Storage::DataStore::AccessMode mode = Sink::Storage::DataStore::ReadOnly);
    ~FulltextIndex();

    void add(const QByteArray &key, const QString &value);
    void add(const QByteArray &key, const QList<QPair<QString, QString>> &values);
    void remove(const QByteArray &key);

    void commitTransaction();
    void abortTransaction();

    QVector<QByteArray> lookup(const QString &key);

    qint64 getDoccount() const;
    struct Result {
        bool found{false};
        QStringList terms;
    };
    Result getIndexContent(const QByteArray &identifier) const;

private:
    Xapian::WritableDatabase* writableDatabase();
    Q_DISABLE_COPY(FulltextIndex);
    Xapian::Database *mDb{nullptr};
    QString mName;
    QString mDbPath;
    bool mHasTransactionOpen{false};
};
