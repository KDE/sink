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

#include "state.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QObject>

#include <iostream>

#ifdef HAVE_LIBGIT2
#include <git2.h>
#endif

static const QString configFileName("hawd.conf");

namespace HAWD
{

State::State(const QString &_configPath)
    : m_valid(true)
{
    m_commitHash[0] = '\0';
    QString configPath = _configPath;
    if (configPath.isEmpty()) {
        QDir dir;

        while (!dir.exists(configFileName) && dir.cdUp()) { }

        if (dir.exists(configFileName)) {
            configPath = dir.absoluteFilePath(configFileName);
        }

        if (configPath.isEmpty()) {
            std::cerr << QObject::tr("Could not find hawd configuration. A hawd.conf file must be in the current directory or in a directory above it.").toStdString() << std::endl;
            m_valid = false;
            return;
        }
    }

    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        QJsonDocument config = QJsonDocument::fromJson(configFile.readAll(), &error);
        if (config.isNull()) {
            std::cerr << QObject::tr("Error parsing config file at %1").arg(configPath).toStdString() << std::endl;
            std::cerr << '\t' << error.errorString().toStdString();
        } else {
            m_configData = config.object();
        }
    }
}

bool State::isValid() const
{
    return m_valid;
}

QString tildeExpand(QString path)
{
    if (path.isEmpty() || path.at(0) != '~') {
        return path;
    }

    return path.replace('~', QDir::homePath());
}

QString State::resultsPath() const
{
    return tildeExpand(configValue("results").toString());
}

QString State::projectPath() const
{
    return tildeExpand(configValue("project").toString());
}

DatasetDefinition State::datasetDefinition(const QString &name) const
{
    return DatasetDefinition(projectPath() + '/' + name);
}

QVariant State::configValue(const QString &key) const
{
    return m_configData.value(key).toVariant();
}

const char *State::commitHash() const
{
    if (isValid() && m_commitHash[0] == '\0') {
        const_cast<State *>(this)->findGitHash();
    }

    return m_commitHash;
}

void State::findGitHash()
{
#ifdef HAVE_LIBGIT2
    git_libgit2_init();
    git_buf root = GIT_BUF_INIT_CONST(0, 0);
    int error = git_repository_discover(&root, projectPath().toStdString().data(), 0, NULL);
    if (!error) {
        git_repository *repo = NULL;
        int error = git_repository_open(&repo, root.ptr);

        if (!error) {
            git_oid oid;
            error = git_reference_name_to_id(&oid, repo, "HEAD" );
            if (!error) {
                git_oid_tostr(m_commitHash, sizeof(m_commitHash), &oid);
            }
        }

        git_repository_free(repo);
    }
    git_buf_free(&root);
    git_libgit2_shutdown();
#endif
}

} // namespace HAWD
