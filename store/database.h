#pragma once

#include <string>
#include <QString>

class Database {
public:
    enum TransactionType { ReadOnly, ReadWrite };

    Database(const QString &path);
    ~Database();
    bool isInTransaction() const;
    bool startTransaction(TransactionType type = ReadWrite);
    bool commitTransaction();
    void abortTransaction();
    bool write(const std::string &sKey, const std::string &sValue);
    //Perhaps prefer iterators (assuming we need to be able to match multiple values
    void read(const std::string &sKey, const std::function<void(const std::string &value)> &);
    void read(const std::string &sKey, const std::function<void(void *ptr, int size)> &);

private:
    class Private;
    Private * const d;
};

