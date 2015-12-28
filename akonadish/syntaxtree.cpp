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

#include "syntaxtree.h"

#include <QCoreApplication>
#include <QDebug>

SyntaxTree *SyntaxTree::s_module = 0;

Syntax::Syntax()
{
}

Syntax::Syntax(const QString &k, const QString &helpText, std::function<bool(const QStringList &, State &)> l, Interactivity inter)
    : keyword(k),
      help(helpText),
      interactivity(inter),
      lambda(l)
{
}

SyntaxTree::SyntaxTree()
{
}

int SyntaxTree::registerSyntax(std::function<Syntax::List()> f)
{
    m_syntax += f();
    return m_syntax.size();
}

SyntaxTree *SyntaxTree::self()
{
    if (!s_module) {
        s_module = new SyntaxTree;
    }

    return s_module;
}

Syntax::List SyntaxTree::syntax() const
{
    return m_syntax;
}

bool SyntaxTree::run(const QStringList &commands)
{
    bool success = false;
    m_timeElapsed.start();
    Command command = match(commands);
    if (command.first) {
        if (command.first->lambda) {
            success = command.first->lambda(command.second, m_state);
            if (success && command.first->interactivity == Syntax::EventDriven) {
                success = m_state.commandStarted();
            }
        } else if (command.first->children.isEmpty()) {
            m_state.printError(QObject::tr("Broken command... sorry :("), "st_broken");
        } else {
            QStringList keywordList;
            for (auto syntax: command.first->children) {
                keywordList << syntax.keyword;
            }
            const QString keywords = keywordList.join(" " );
            m_state.printError(QObject::tr("Command requires additional arguments, one of: %1").arg(keywords));
        }
    } else {
        m_state.printError(QObject::tr("Unknown command"), "st_unknown");
    }

    if (m_state.commandTiming()) {
        m_state.printLine(QObject::tr("Time elapsed: %1").arg(m_timeElapsed.elapsed()));
    }
    return false;
}

SyntaxTree::Command SyntaxTree::match(const QStringList &commandLine) const
{
    if (commandLine.isEmpty()) {
        return Command();
    }

    QStringListIterator commandLineIt(commandLine);

    QVectorIterator<Syntax> syntaxIt(m_syntax);
    const Syntax *lastFullSyntax = 0;
    QStringList tailCommands;
    while (commandLineIt.hasNext() && syntaxIt.hasNext()) {
        const QString word = commandLineIt.next();
        while (syntaxIt.hasNext()) {
            const Syntax &syntax = syntaxIt.next();
            if (word == syntax.keyword) {
                lastFullSyntax = &syntax;
                syntaxIt = syntax.children;
                break;
            }
        }
    }

    if (lastFullSyntax) {
        while (commandLineIt.hasNext()) {
            tailCommands << commandLineIt.next();
        }

        return std::make_pair(lastFullSyntax, tailCommands);
    }

    return Command();
}

Syntax::List SyntaxTree::nearestSyntax(const QStringList &words, const QString &fragment) const
{
    Syntax::List matches;

    //qDebug() << "words are" << words;
    if (words.isEmpty()) {
        for (const Syntax &syntax: m_syntax) {
            if (syntax.keyword.startsWith(fragment)) {
                matches.push_back(syntax);
            }
        }
    } else {
        QStringListIterator wordIt(words);
        QVectorIterator<Syntax> syntaxIt(m_syntax);
        Syntax lastFullSyntax;

        while (wordIt.hasNext()) {
            const QString &word = wordIt.next();
            while (syntaxIt.hasNext()) {
                const Syntax &syntax = syntaxIt.next();
                if (word == syntax.keyword) {
                    lastFullSyntax = syntax;
                    syntaxIt = syntax.children;
                    break;
                }
            }
        }

        //qDebug() << "exiting with" << lastFullSyntax.keyword << words.last();
        if (lastFullSyntax.keyword == words.last()) {
            syntaxIt = lastFullSyntax.children;
            while (syntaxIt.hasNext()) {
                Syntax syntax = syntaxIt.next();
                if (fragment.isEmpty() || syntax.keyword.startsWith(fragment)) {
                    matches.push_back(syntax);
                }
            }
        }
    }

    return matches;
}

State &SyntaxTree::state()
{
    return m_state;
}

QStringList SyntaxTree::tokenize(const QString &text)
{
    //TODO: properly tokenize (e.g. "foo bar" should not become ['"foo', 'bar"']a
    static const QVector<QChar> quoters = QVector<QChar>() << '"' << '\'';
    QStringList tokens;
    QString acc;
    QChar closer;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == '\\') {
            ++i;
            if (i < text.size()) {
                acc.append(text.at(i));
            }
        } else if (!closer.isNull()) {
            if (c == closer) {
                acc = acc.trimmed();
                if (!acc.isEmpty()) {
                    tokens << acc;
                }
                acc.clear();
                closer = QChar();
            } else {
                acc.append(c);
            }
        } else if (c.isSpace()) {
            acc = acc.trimmed();
            if (!acc.isEmpty()) {
                tokens << acc;
            }
            acc.clear();
        } else if (quoters.contains(c)) {
            closer = c;
        } else {
            acc.append(c);
        }
    }

    acc = acc.trimmed();
    if (!acc.isEmpty()) {
        tokens << acc;
    }

    return tokens;
}

