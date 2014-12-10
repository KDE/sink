#pragma once

#include <string>
#include <QString>

class Storage {
public:
    enum AccessMode { ReadOnly, ReadWrite };

    class Error
    {
    public:
        Error(const std::string &s, int c, const std::string &m)
            : store(s), message(m), code(c) {}
        std::string store;
        std::string message;
        int code;
    };

    Storage(const QString &storageRoot, const QString &name, AccessMode mode = ReadOnly);
    ~Storage();
    bool isInTransaction() const;
    bool startTransaction(AccessMode mode = ReadWrite);
    bool commitTransaction();
    void abortTransaction();
    //TODO: row removal
    //TODO: cursor based read
    //TODO: query?
    bool write(const char *key, size_t keySize, const char *value, size_t valueSize);
    bool write(const std::string &sKey, const std::string &sValue);
    void read(const std::string &sKey,
              const std::function<bool(const std::string &value)> &resultHandler);
    void read(const std::string &sKey,
              const std::function<bool(const std::string &value)> &resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);
    void read(const std::string &sKey, const std::function<bool(void *ptr, int size)> &resultHandler);
    void read(const std::string &sKey,
              const std::function<bool(void *ptr, int size)> & resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);
    void scan(const std::string &sKey, const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler);
    void scan(const char *keyData, uint keySize,
              const std::function<bool(void *keyPtr, int keySize, void *ptr, int size)> &resultHandler,
              const std::function<void(const Storage::Error &error)> &errorHandler);
    void readAll(const std::function<bool(void *key, int keySize, void *data, int dataSize)> &resultHandler,
                 const std::function<void(const Storage::Error &error)> &errorHandler);

    static std::function<void(const Storage::Error &error)> basicErrorHandler();
    qint64 diskUsage() const;
    void removeFromDisk() const;
private:
    class Private;
    Private * const d;
};

