#pragma once

#include <QObject>
#include <QByteArrayList>
#include <string>
#include <functional>
#include <QString>
#include <Async/Async>
#include "storage.h"

/**
 * A persistent FIFO message queue.
 */
class MessageQueue : public QObject
{
    Q_OBJECT
public:
    enum ErrorCodes {
        NoMessageFound
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

    MessageQueue(const QString &storageRoot, const QString &name);
    ~MessageQueue();

    void startTransaction();
    void enqueue(void const *msg, size_t size);
    void enqueue(const QByteArray &value);
    //Dequeue a message. This will return a new message everytime called.
    //Call the result handler with a success response to remove the message from the store.
    //TODO track processing progress to avoid processing the same message with the same preprocessor twice?
    void dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> & resultHandler,
              const std::function<void(const Error &error)> &errorHandler);
    KAsync::Job<void> dequeueBatch(int maxBatchSize, const std::function<KAsync::Job<void>(const QByteArray &)> &resultHandler);
    bool isEmpty();

public slots:
    void commit();

signals:
    void messageReady();
    void drained();

private slots:
    void processRemovals();

private:
    Q_DISABLE_COPY(MessageQueue);
    Akonadi2::Storage mStorage;
    Akonadi2::Storage::Transaction mWriteTransaction;
    QByteArrayList mPendingRemoval;
};
