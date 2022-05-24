#pragma once

#include "sink_export.h"

#include <string>
#include <functional>
#include <QString>
#include <QDateTime>
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

    static bool exists(const QByteArray &resourceInstanceIdentifier);

    void add(const Sink::Storage::Identifier &key, const QString &value);
    void add(const Sink::Storage::Identifier &key, const QList<QPair<QString, QString>> &values, const QDateTime &date = {});
    void remove(const Sink::Storage::Identifier &key);

    void commitTransaction();
    void abortTransaction();

    QVector<Sink::Storage::Identifier> lookup(const QString &key, const Sink::Storage::Identifier &id = {});

    qint64 getDoccount() const;
    struct Result {
        bool found{false};
        QStringList terms;
    };
    Result getIndexContent(const Sink::Storage::Identifier &identifier) const;
    Result getIndexContent(const QByteArray &identifier) const
    {
        return getIndexContent(Sink::Storage::Identifier::fromDisplayByteArray(identifier));
    }

private:
    Xapian::WritableDatabase* writableDatabase();
    Q_DISABLE_COPY(FulltextIndex);
    Xapian::Database *mDb{nullptr};
    QString mName;
    QString mDbPath;
    bool mHasTransactionOpen{false};
};
