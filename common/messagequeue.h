/*
 * Copyright (C) 2019 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "sink_export.h"
#include <QObject>
#include <QByteArrayList>
#include <string>
#include <functional>
#include <QString>
#include <KAsync/Async>
#include "storage.h"

/**
 * A persistent FIFO message queue.
 */
class SINK_EXPORT MessageQueue : public QObject
{
    Q_OBJECT
public:
    enum ErrorCodes
    {
        NoMessageFound
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

    MessageQueue(const QString &storageRoot, const QString &name);
    ~MessageQueue();

    void startTransaction();
    void enqueue(void const *msg, size_t size);
    void enqueue(const QByteArray &value);
    // Dequeue a message. This will return a new message everytime called.
    // Call the result handler with a success response to remove the message from the store.
    // TODO track processing progress to avoid processing the same message with the same preprocessor twice?
    void dequeue(const std::function<void(void *ptr, int size, std::function<void(bool success)>)> &resultHandler, const std::function<void(const Error &error)> &errorHandler);
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
    Sink::Storage::DataStore mStorage;
    Sink::Storage::DataStore::Transaction mWriteTransaction;
    qint64 mReplayedRevision;
};
