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

#include "resource.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>
#include <QPointer>

namespace Akonadi2
{

Resource::Resource()
    : d(0)
{

}

Resource::~Resource()
{
    //delete d;
}

void Resource::configurePipeline(Pipeline *pipeline)
{

}

void Resource::processCommand(int commandId, const QByteArray &data, uint size, Pipeline *pipeline)
{
    Q_UNUSED(commandId)
    Q_UNUSED(data)
    Q_UNUSED(size)
    Q_UNUSED(pipeline)
    pipeline->null();
}

Async::Job<void> Resource::synchronizeWithSource(Pipeline *pipeline)
{
    return Async::start<void>([pipeline](Async::Future<void> &f) {
        pipeline->null();
    });
}

class ResourceFactory::Private
{
public:
    static QHash<QString, QPointer<ResourceFactory> > s_loadedFactories;
};

QHash<QString, QPointer<ResourceFactory> > ResourceFactory::Private::s_loadedFactories;

ResourceFactory::ResourceFactory(QObject *parent)
    : QObject(parent),
      d(0)
{

}

ResourceFactory::~ResourceFactory()
{
    //delete d;
}

ResourceFactory *ResourceFactory::load(const QString &resourceName)
{
    ResourceFactory *factory = Private::s_loadedFactories.value(resourceName);
    if (factory) {
        return factory;
    }

    for (auto const &path: QCoreApplication::instance()->libraryPaths()) {
        if (path.endsWith(QLatin1String("plugins"))) {
            QDir pluginDir(path);
            //TODO: centralize this so that it is easy to change centrally
            //      also ref'd in cmake as ${AKONADI_RESOURCE_PLUGINS_PATH}
            pluginDir.cd(QStringLiteral("akonadi2"));
            pluginDir.cd(QStringLiteral("resources"));

            for (const QString &fileName: pluginDir.entryList(QDir::Files)) {
                const QString path = pluginDir.absoluteFilePath(fileName);
                QPluginLoader loader(path);

                const QString id = loader.metaData()[QStringLiteral("IID")].toString();
                if (id == resourceName) {
                    QObject *object = loader.instance();
                    if (object) {
                        factory = qobject_cast<ResourceFactory *>(object);
                        if (factory) {
                            Private::s_loadedFactories.insert(resourceName, factory);
                            factory->registerFacades(FacadeFactory::instance());
                            //TODO: if we need more data on it const QJsonObject json = loader.metaData()[QStringLiteral("MetaData")].toObject();
                            return factory;
                        } else {
                            qWarning() << "Plugin for" << resourceName << "from plugin" << loader.fileName() << "produced the wrong object type:" << object;
                            delete object;
                        }
                    } else {
                        qWarning() << "Could not load factory for" << resourceName << "from plugin" << loader.fileName() << "due to the following error:" << loader.errorString();
                    }
                }
            }
        }
    }

    qWarning() << "Failed to find factory for resource:" << resourceName;
    return nullptr;
}

} // namespace Akonadi2
