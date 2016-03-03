#pragma once

#include <QList>
#include <functional>
#include <QSharedPointer>
#include <QEventLoop>
#include <QDebug>
#include "resultprovider.h"

namespace async {

/*
* A result set specialization that provides a syncronous list.
*
* Only for testing purposes.
*
* WARNING: The nested eventloop can cause all sorts of trouble. Use only in testing code.
*/
template <class T>
class SyncListResult : public QList<T>
{
public:
    SyncListResult(const QSharedPointer<Sink::ResultEmitter<T>> &emitter) : QList<T>(), mEmitter(emitter)
    {
        emitter->onAdded([this](const T &value) { this->append(value); });
        emitter->onModified([this](const T &value) {
            for (auto it = this->begin(); it != this->end(); it++) {
                if (**it == *value) {
                    it = this->erase(it);
                    this->insert(it, value);
                    break;
                }
            }
        });
        emitter->onRemoved([this](const T &value) {
            for (auto it = this->begin(); it != this->end(); it++) {
                if (**it == *value) {
                    this->erase(it);
                    break;
                }
            }
        });
        emitter->onInitialResultSetComplete([this]() {
            if (eventLoopAborter) {
                eventLoopAborter();
                // Be safe in case of a second invocation of the complete handler
                eventLoopAborter = std::function<void()>();
            }
        });
        emitter->onComplete([this]() { mEmitter.clear(); });
        emitter->onClear([this]() { this->clear(); });
    }

    void exec()
    {
        QEventLoop eventLoop;
        eventLoopAborter = [&eventLoop]() { eventLoop.quit(); };
        eventLoop.exec();
    }

private:
    QSharedPointer<Sink::ResultEmitter<T>> mEmitter;
    std::function<void()> eventLoopAborter;
};
}
