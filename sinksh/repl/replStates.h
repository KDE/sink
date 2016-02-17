/*
 * Copyright (C) 2014 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#pragma once

#include <QState>

class QSocketNotifier;

class ReadState : public QState
{
    Q_OBJECT

public:
    ReadState(QState *parent = 0);

signals:
    void command(const QString &command);
    void exitRequested();

protected:
    void onEntry(QEvent *event);
    virtual const char *prompt() const;
};

class UnfinishedReadState : public ReadState
{
    Q_OBJECT

public:
    UnfinishedReadState(QState *parent = 0);

protected:
    const char *prompt() const;
};

class EvalState : public QState
{
    Q_OBJECT

public:
    EvalState(QState *parent = 0);

signals:
    void completed();
    void continueInput();
    void output(const QString &output);

protected:
    void onEntry(QEvent *event);

private:
    void complete();

    QString m_partial;
};

class PrintState : public QState
{
    Q_OBJECT

public:
    PrintState(QState *parent = 0);

signals:
    void completed();

protected:
    void onEntry(QEvent *event);
};

