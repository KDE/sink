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
    : keyword(k), help(helpText), interactivity(inter), lambda(l)
{
}

void Syntax::addPositionalArgument(const Argument &argument)
{
    arguments.push_back(argument);
}

void Syntax::addParameter(const QString &name, const ParameterOptions &options)
{
    parameters.insert(name, options);
}

void Syntax::addFlag(const QString &name, const QString &help)
{
    flags.insert(name, help);
}

QString Syntax::usage() const
{
    // TODO: refactor into meaningful functions?
    bool hasArguments = !arguments.isEmpty();
    bool hasFlags = !flags.isEmpty();
    bool hasOptions = !parameters.isEmpty();
    bool hasSubcommand = !children.isEmpty();

    QString argumentsSummary;

    QString argumentsUsage;
    if (hasArguments) {
        argumentsUsage += "\nARGUMENTS:\n";
        for (const auto &arg : arguments) {
            if (arg.required) {
                argumentsSummary += QString(" <%1>").arg(arg.name);
                argumentsUsage += QString("    <%1>: %2\n").arg(arg.name).arg(arg.help);
            } else {
                argumentsSummary += QString(" [%1]").arg(arg.name);
                argumentsUsage += QString("    [%1]: %2\n").arg(arg.name).arg(arg.help);
            }
            if (arg.variadic) {
                argumentsSummary += "...";
            }
        }
    }

    if (hasFlags) {
        argumentsSummary += " [FLAGS]";
    }

    if (hasOptions) {
        argumentsSummary += " [OPTIONS]";
    }

    if (hasSubcommand) {
        if (hasArguments || hasFlags || hasOptions) {
            argumentsSummary = QString(" [ <SUB-COMMAND> |%1 ]").arg(argumentsSummary);
        } else {
            argumentsSummary = " <SUB-COMMAND>";
        }
    }

    argumentsSummary += '\n';

    QString subcommandsUsage;
    if (hasSubcommand) {
        subcommandsUsage += "\nSUB-COMMANDS:\n"
                            "    Use the 'help' command to find out more about a sub-command.\n\n";
        for (const auto &command : children) {
            subcommandsUsage += QString("    %1: %2\n").arg(command.keyword).arg(command.help);
        }
    }

    QString flagsUsage;
    if (hasFlags) {
        flagsUsage += "\nFLAGS:\n";
        for (auto it = flags.constBegin(); it != flags.constEnd(); ++it) {
            flagsUsage += QString("    [--%1]: %2\n").arg(it.key()).arg(it.value());
        }
    }

    QString optionsUsage;
    if (hasOptions) {
        optionsUsage += "\nOPTIONS:\n";
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
            optionsUsage += "    ";
            if (!it.value().required) {
                optionsUsage += QString("[--%1 $%2]").arg(it.key()).arg(it.value().name);
            } else {
                optionsUsage += QString("<--%1 $%2>").arg(it.key()).arg(it.value().name);
            }

            optionsUsage += ": " + it.value().help + '\n';
        }
    }

    // TODO: instead of just the keyword, we might want to have the whole
    // command (e.g. if this is a sub-command)
    return QString("USAGE:\n    ") + keyword + argumentsSummary + subcommandsUsage +
           argumentsUsage + flagsUsage + optionsUsage;
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

int SyntaxTree::run(const QStringList &commands)
{
    int returnCode = 0;
    m_timeElapsed.start();
    Command command = match(commands);
    if (command.first) {
        if (command.first->lambda) {
            bool success = command.first->lambda(command.second, m_state);
            if (success && command.first->interactivity == Syntax::EventDriven) {
                returnCode = m_state.commandStarted();
            }
            if (!success && command.first->interactivity != Syntax::EventDriven) {
                returnCode = 1;
            }
        } else if (command.first->children.isEmpty()) {
            m_state.printError(QObject::tr("Broken command... sorry :("), "st_broken");
        } else {
            QStringList keywordList;
            for (auto syntax : command.first->children) {
                keywordList << syntax.keyword;
            }
            const QString keywords = keywordList.join(" ");
            m_state.printError(QObject::tr("Command requires additional arguments, one of: %1").arg(keywords));
        }
    } else {
        m_state.printError(QObject::tr("Unknown command"), "st_unknown");
    }

    if (m_state.commandTiming()) {
        m_state.printLine(QObject::tr("Time elapsed: %1").arg(m_timeElapsed.elapsed()));
    }
    return returnCode;
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
        bool match = false;
        while (syntaxIt.hasNext()) {
            const Syntax &syntax = syntaxIt.next();
            if (word == syntax.keyword) {
                lastFullSyntax = &syntax;
                syntaxIt = syntax.children;
                match = true;
                break;
            }
        }
        if (!match) {
            //Otherwise we would miss the just evaluated command from the tailCommands
            if (commandLineIt.hasPrevious()) {
                commandLineIt.previous();
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

    // qDebug() << "words are" << words;
    if (words.isEmpty()) {
        for (const Syntax &syntax : m_syntax) {
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

        // qDebug() << "exiting with" << lastFullSyntax.keyword << words.last();
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
    // TODO: properly tokenize (e.g. "foo bar" should not become ['"foo', 'bar"']a
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

SyntaxTree::Options SyntaxTree::parseOptions(const QStringList &args)
{
    Options result;
    auto it = args.constBegin();
    for (;it != args.constEnd(); it++) {
        if (it->startsWith("--")) {
            QString option = it->mid(2);
            QStringList list;
            it++;
            for (;it != args.constEnd(); it++) {
                if (it->startsWith("--")) {
                    it--;
                    break;
                }
                list << *it;
            }
            result.options.insert(option, list);
            if (it == args.constEnd()) {
                break;
            }
        } else {
            result.positionalArguments << *it;
        }
    }
    return result;
}
