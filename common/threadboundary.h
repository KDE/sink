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

#include "sink_export.h"

#include <QObject>
#include <functional>

namespace async {

/*
* A helper class to invoke a method in a different thread using the event loop.
* The ThreadBoundary object must live in the thread where the function should be called.
*/
class SINK_EXPORT ThreadBoundary : public QObject {
    Q_OBJECT
public:
    ThreadBoundary();
    virtual ~ThreadBoundary();

    //Call in worker thread
    void callInMainThread(std::function<void()> f);
public slots:
    //Get's called in main thread by it's eventloop
    void runInMainThread(std::function<void()> f);
};

}
