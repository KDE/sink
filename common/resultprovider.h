/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include <functional>
#include <memory>
#include "threadboundary.h"

using namespace async;

namespace Akonadi2 {

/**
* Query result set
*/
template<class T>
class ResultEmitter;

/*
* The promise side for the result emitter
*/
template<class T>
class ResultProvider {
private:
    void callInMainThreadOnEmitter(void (ResultEmitter<T>::*f)())
    {
        //We use the eventloop to call the addHandler directly from the main eventloop.
        //That way the result emitter implementation doesn't have to care about threadsafety at all.
        //The alternative would be to make all handlers of the emitter threadsafe.
        if (auto emitter = mResultEmitter.toStrongRef()) {
            auto weakEmitter = mResultEmitter;
            //We don't want to keep the emitter alive here, so we only capture a weak reference
            emitter->mThreadBoundary.callInMainThread([weakEmitter, f]() {
                if (auto strongRef = weakEmitter.toStrongRef()) {
                    (strongRef.data()->*f)();
                }
            });
        }
    }

    void callInMainThreadOnEmitter(const std::function<void()> &f)
    {
        //We use the eventloop to call the addHandler directly from the main eventloop.
        //That way the result emitter implementation doesn't have to care about threadsafety at all.
        //The alternative would be to make all handlers of the emitter threadsafe.
        if (auto emitter = mResultEmitter.toStrongRef()) {
            emitter->mThreadBoundary.callInMainThread([f]() {
                f();
            });
        }
    }

public:
    //Called from worker thread
    void add(const T &value)
    {
        //Because I don't know how to use bind
        auto weakEmitter = mResultEmitter;
        callInMainThreadOnEmitter([weakEmitter, value](){
            if (auto strongRef = weakEmitter.toStrongRef()) {
                strongRef->addHandler(value);
            }
        });
    }

    void initialResultSetComplete()
    {
        callInMainThreadOnEmitter(&ResultEmitter<T>::initialResultSetComplete);
    }

    //Called from worker thread
    void complete()
    {
        callInMainThreadOnEmitter(&ResultEmitter<T>::complete);
    }

    void clear()
    {
        callInMainThreadOnEmitter(&ResultEmitter<T>::clear);
    }


    QSharedPointer<ResultEmitter<T> > emitter()
    {
        if (!mResultEmitter) {
            //We have to go over a separate var and return that, otherwise we'd delete the emitter immediately again
            auto sharedPtr = QSharedPointer<ResultEmitter<T> >(new ResultEmitter<T>, [this](ResultEmitter<T> *emitter){ mThreadBoundary->callInMainThread([this]() {done();}); delete emitter; });
            mResultEmitter = sharedPtr;
            return sharedPtr;
        }

        return mResultEmitter.toStrongRef();
    }

    /**
        * For lifetimemanagement only.
        * We keep the runner alive as long as the result provider exists.
        */
    void setQueryRunner(const QSharedPointer<QObject> &runner)
    {
        mQueryRunner = runner;
    }

    /**
        * For lifetimemanagement only.
        * We keep the runner alive as long as the result provider exists.
        */
    void setFacade(const std::shared_ptr<void> &facade)
    {
        mFacade = facade;
    }

    void onDone(const std::function<void()> &callback)
    {
        mThreadBoundary = QSharedPointer<ThreadBoundary>::create();
        mOnDoneCallback = callback;
    }

    bool isDone() const
    {
        //The existance of the emitter currently defines wether we're done or not.
        return mResultEmitter.toStrongRef().isNull();
    }

private:
    void done()
    {
        qWarning() << "done";
        if (mOnDoneCallback) {
            mOnDoneCallback();
            mOnDoneCallback = std::function<void()>();
        }
    }

    QWeakPointer<ResultEmitter<T> > mResultEmitter;
    QSharedPointer<QObject> mQueryRunner;
    std::shared_ptr<void> mFacade;
    std::function<void()> mOnDoneCallback;
    QSharedPointer<ThreadBoundary> mThreadBoundary;
};

/*
* The future side for the client.
*
* It does not directly hold the state.
*
* The advantage of this is that we can specialize it to:
* * do inline transformations to the data
* * directly store the state in a suitable datastructure: QList, QSet, std::list, QVector, ...
* * build async interfaces with signals
* * build sync interfaces that block when accessing the value
*
*/
template<class DomainType>
class ResultEmitter {
public:
    void onAdded(const std::function<void(const DomainType&)> &handler)
    {
        addHandler = handler;
    }
    // void onRemoved(const std::function<void(const T&)> &handler);
    void onInitialResultSetComplete(const std::function<void(void)> &handler)
    {
        initialResultSetCompleteHandler = handler;
    }
    void onComplete(const std::function<void(void)> &handler)
    {
        completeHandler = handler;
    }
    void onClear(const std::function<void(void)> &handler)
    {
        clearHandler = handler;
    }

    void add(const DomainType &value)
    {
        addHandler(value);
    }

    void initialResultSetComplete()
    {
        initialResultSetCompleteHandler();
    }

    void complete()
    {
        completeHandler();
    }

    void clear()
    {
        clearHandler();
    }

private:
    friend class ResultProvider<DomainType>;

    std::function<void(const DomainType&)> addHandler;
    // std::function<void(const T&)> removeHandler;
    std::function<void(void)> initialResultSetCompleteHandler;
    std::function<void(void)> completeHandler;
    std::function<void(void)> clearHandler;
    ThreadBoundary mThreadBoundary;
};

}

