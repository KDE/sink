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

#include "threadboundary.h"
#include <QThread>

Q_DECLARE_METATYPE(std::function<void()>);

namespace async {
ThreadBoundary::ThreadBoundary() : QObject()
{
    qRegisterMetaType<std::function<void()>>("std::function<void()>");
}

ThreadBoundary::~ThreadBoundary()
{
}

void ThreadBoundary::callInMainThread(std::function<void()> f)
{
    /*
     * This implementation causes lambdas to pile up if the target thread is the same as the caller thread, or the caller thread calls faster
     * than the target thread is able to execute the function calls. In that case any captures will equally pile up, resulting
     * in significant memory usage i.e. due to Emitter::addHandler calls that each capture a domain object.
     */
    if (QThread::currentThread() == this->thread()) {
        f();
    } else {
        QMetaObject::invokeMethod(this, "runInMainThread", Qt::QueuedConnection, QGenericReturnArgument(), Q_ARG(std::function<void()>, f));
    }
}

void ThreadBoundary::runInMainThread(std::function<void()> f)
{
    f();
}
}
