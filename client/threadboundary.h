#pragma once

#include <QObject>
#include <functional>

namespace async {
    /*
     * A helper class to invoke a method in a different thread using the event loop.
     * The ThreadBoundary object must live in the thread where the function should be called.
     */
    class ThreadBoundary : public QObject {
        Q_OBJECT
    public:
        ThreadBoundary();
        virtual ~ThreadBoundary();

        //Call in worker thread
        void callInMainThread(std::function<void()> f) {
            QMetaObject::invokeMethod(this, "addValueInMainThread", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(std::function<void()>, f));
        }
    public slots:
        //Get's called in main thread by it's eventloop
        void addValueInMainThread(std::function<void()> f) {
            f();
        }
    };
}
