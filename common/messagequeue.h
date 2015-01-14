#pragma once

#include <QObject>
#include <string>
#include <functional>
#include <QString>
#include "storage.h"

/**
 * A persistent FIFO message queue.
 */
class MessageQueue : public QObject
{
    Q_OBJECT
public:
    class Error
    {
    public:
        Error(const std::string &s, int c, const std::string &m)
            : store(s), message(m), code(c) {}
        std::string store;
        std::string message;
        int code;
    };

    MessageQueue(const QString &storageRoot, const QString &name);

    void enqueue(void const *msg, size_t size);
    //Dequeue a message. This will return a new message everytime called.
    //Call the result handler with a success response to remove the message from the store.
    //TODO track processing progress to avoid processing the same message with the same preprocessor twice?
    void dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> & resultHandler,
              const std::function<void(const Error &error)> &errorHandler);
    bool isEmpty();
signals:
    void messageReady();

private:
    Akonadi2::Storage mStorage;
};
