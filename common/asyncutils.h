/*
    Copyright (c) 2015 Christian Mollekopf <mollekopf@kolabsys.com>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/
#pragma once

#include <KAsync/Async>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QFutureWatcher>

namespace async {
template <typename T>
KAsync::Job<T> run(const std::function<T()> &f, bool runAsync = true)
{
    if (runAsync) {
        return KAsync::start<T>([f](KAsync::Future<T> &future) {
            auto result = QtConcurrent::run(f);
            auto watcher = new QFutureWatcher<T>;
            watcher->setFuture(result);
            QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher, [&future, watcher]() {
                future.setValue(watcher->future().result());
                delete watcher;
                future.setFinished();
            });
        });
    } else {
        return KAsync::syncStart<T>([f]() {
            return f();
        });
    }
}
}
