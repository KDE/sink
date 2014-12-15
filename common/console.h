/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
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

#include <akonadi2common_export.h>

#include <QTime>
#include <QWidget>

class QTextBrowser;

namespace Akonadi2
{

class AKONADI2COMMON_EXPORT Console : public QWidget
{
    Q_OBJECT
public:
    static Console *main();
    Console(const QString &title);
    ~Console();

    void log(const QString &message);

private:
    QTextBrowser *m_textDisplay;
    QTime m_timestamper;
    static Console *s_output;
};

} // namespace Akonadi2
