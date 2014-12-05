#pragma once

#include <string>
#include <QString>

class Storage {
public:
    enum TransactionType { ReadOnly, ReadWrite };

    Storage(const QString &storageRoot, const QString &name);
    ~Storage();
    bool isInTransaction() const;
    bool startTransaction(TransactionType type = ReadWrite);
    bool commitTransaction();
    void abortTransaction();
    bool write(const char *key, size_t keySize, const char *value, size_t valueSize);
    bool write(const std::string &sKey, const std::string &sValue);
    //Perhaps prefer iterators (assuming we need to be able to match multiple values
    void read(const std::string &sKey, const std::function<void(const std::string &value)> &);
    void read(const std::string &sKey, const std::function<void(void *ptr, int size)> &);

    qint64 diskUsage() const;
    void removeFromDisk() const;
private:
    class Private;
    Private * const d;
};

