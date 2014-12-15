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

#include "console.h"

#include <QFontDatabase>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Akonadi2
{

static Console *s_console = 0;

Console *Console::main()
{
    if (!s_console) {
        s_console = new Console(QString());
    }
    return s_console;
}

Console::Console(const QString &title)
    : QWidget(0)
{
    if (!s_console) {
        s_console = this;
    }

    resize(1000, 1500);

    QVBoxLayout *topLayout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel(this);
    titleLabel->setText(title);
    QFont font = titleLabel->font();
    font.setWeight(QFont::Bold);
    titleLabel->setFont(font);
    titleLabel->setAlignment(Qt::AlignCenter);

    QFont consoleFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    consoleFont.setPointSize(7);
    m_textDisplay = new QTextBrowser(this);
    m_textDisplay->document()->setDefaultFont(consoleFont);
    topLayout->addWidget(titleLabel);
    topLayout->addWidget(m_textDisplay, 10);

    show();
    m_timestamper.start();
}

Console::~Console()
{

}

void Console::log(const QString &message)
{
    m_textDisplay->append(QString::number(m_timestamper.elapsed()).rightJustified(6) + ": " + message);
}

} // namespace Akonadi2
