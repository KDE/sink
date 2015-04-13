#pragma once

#include <QList>
#include <functional>
#include <QSharedPointer>
#include <clientapi.h>

namespace async {

/*
* A result set specialization that provides a syncronous list.
*
* Only for testing purposes.
*
* WARNING: The nested eventloop can cause all sorts of trouble. Use only in testing code.
*/
template<class T>
class SyncListResult : public QList<T> {
public:
    SyncListResult(const QSharedPointer<ResultEmitter<T> > &emitter)
        :QList<T>(),
        mComplete(false),
        mEmitter(emitter)
    {
        emitter->onAdded([this](const T &value) {
            this->append(value);
        });
        emitter->onComplete([this]() {
            mComplete = true;
            if (eventLoopAborter) {
                eventLoopAborter();
                //Be safe in case of a second invocation of the complete handler
                eventLoopAborter = std::function<void()>();
            }
        });
        emitter->onClear([this]() {
            this->clear();
        });
    }

    void exec()
    {
        QEventLoop eventLoop;
        eventLoopAborter = [&eventLoop]() { eventLoop.quit(); };
        eventLoop.exec();
    }

private:
    bool mComplete;
    QSharedPointer<ResultEmitter<T> > mEmitter;
    std::function<void()> eventLoopAborter;
};

}
