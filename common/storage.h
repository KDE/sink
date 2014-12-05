#pragma once

#include <string>
#include <QString>

class Storage {
public:
    enum AccessMode { ReadOnly, ReadWrite };

    Storage(const QString &storageRoot, const QString &name, AccessMode mode = ReadOnly);
    ~Storage();
    bool isInTransaction() const;
    bool startTransaction(AccessMode mode = ReadWrite);
    bool commitTransaction();
    void abortTransaction();
    bool write(const char *key, size_t keySize, const char *value, size_t valueSize);
    bool write(const std::string &sKey, const std::string &sValue);
    //Perhaps prefer iterators (assuming we need to be able to match multiple values
    bool read(const std::string &sKey, const std::function<void(const std::string &value)> &);
    bool read(const std::string &sKey, const std::function<void(void *ptr, int size)> &);

    qint64 diskUsage() const;
    void removeFromDisk() const;
private:
    class Private;
    Private * const d;
};

