/*
    Copyright (c) 2007 Till Adam <adam@kde.org>

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

#include "maildir.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHostInfo>
#include <QUuid>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log, "maildir");

#include <time.h>
#include <unistd.h>

bool removeDirAndContentsRecursively(const QString & path)
{
    bool success = true;

    QDir d;
    d.setPath(path);
    d.setFilter(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoSymLinks);

    QFileInfoList list = d.entryInfoList();

    Q_FOREACH (const QFileInfo &fi, list) {
        if (fi.isDir()) {
            if (fi.fileName() != QLatin1String(".") && fi.fileName() != QLatin1String("..")) {
                success = success && removeDirAndContentsRecursively(fi.absoluteFilePath());
            }
        } else {
        success = success && d.remove(fi.absoluteFilePath());
        }
    }

    if (success) {
        success = success && d.rmdir(path); // nuke ourselves, we should be empty now
    }
    return success;
}

using namespace KPIM;

Q_GLOBAL_STATIC_WITH_ARGS(QRegExp, statusSeparatorRx, (":|!"))

class Maildir::Private
{
public:
    Private(const QString& p, bool isRoot)
        :path(p), isRoot(isRoot)
    {
      hostName = QHostInfo::localHostName();
    }

    Private(const Private& rhs)
    {
        path = rhs.path;
        isRoot = rhs.isRoot;
        hostName = rhs.hostName;
    }

    bool operator==(const Private& rhs) const
    {
        return path == rhs.path;
    }
    bool accessIsPossible(bool createMissingFolders = true);
    bool canAccess(const QString& path) const;

    QStringList subPaths() const
    {
        QStringList paths;
        paths << path + QString::fromLatin1("/cur");
        paths << path + QString::fromLatin1("/new");
        paths << path + QString::fromLatin1("/tmp");
        return paths;
    }

    QStringList listNew() const
    {
        QDir d(path + QString::fromLatin1("/new"));
        d.setSorting(QDir::NoSort);
        return d.entryList(QDir::Files);
    }

    QStringList listCurrent() const
    {
        QDir d(path + QString::fromLatin1("/cur"));
        d.setSorting(QDir::NoSort);
        return d.entryList(QDir::Files);
    }

    QString findRealKey(const QString& key) const
    {
        if (key.isEmpty()) {
            qCWarning(log) << "Empty key: " << key;
            return key;
        }
        if (QFile::exists(path + QString::fromLatin1("/cur/") + key)) {
          return path + QString::fromLatin1("/cur/") + key;
        }
        if (QFile::exists(path + QString::fromLatin1("/new/") + key)) {
          return path + QString::fromLatin1("/new/") + key;
        }
        {
            QDir dir(path + QString::fromLatin1("/cur/"));
            const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
            if (!list.isEmpty()) {
                return list.first().filePath();
            }
        }

        {
            QDir dir(path + QString::fromLatin1("/new/"));
            const QFileInfoList list = dir.entryInfoList(QStringList() << (key+"*"), QDir::Files);
            if (!list.isEmpty()) {
                return list.first().filePath();
            }
        }

        return QString();
    }

    static QString stripFlags(const QString& key)
    {
        const QRegExp rx = *(statusSeparatorRx());
        const int index = key.indexOf(rx);
        return key.mid(0, index);
    }

    static QString subDirNameForFolderName(const QString &folderName)
    {
        return QString::fromLatin1(".%1.directory").arg(folderName);
    }

    QString subDirPath() const
    {
        QDir dir(path);
        return subDirNameForFolderName(dir.dirName());
    }

    bool moveAndRename(QDir &dest, const QString &newName)
    {
      if (!dest.exists()) {
        qCDebug(log) << "Destination does not exist";
        return false;
      }
      if (dest.exists(newName) || dest.exists(subDirNameForFolderName(newName))) {
        qCDebug(log) << "New name already in use";
        return false;
      }

      if (!dest.rename(path, newName)) {
        qCDebug(log) << "Failed to rename maildir";
        return false;
      }
      const QDir subDirs(Maildir::subDirPathForFolderPath(path));
      if (subDirs.exists() && !dest.rename(subDirs.path(), subDirNameForFolderName(newName))) {
        qCDebug(log) << "Failed to rename subfolders";
        return false;
      }

      path = dest.path() + QDir::separator() + newName;
      return true;
    }

    QString path;
    bool isRoot;
    QString hostName;
};

Maildir::Maildir(const QString& path, bool isRoot)
:d(new Private(path, isRoot))
{
}

void Maildir::swap(const Maildir &rhs)
{
    Private *p = d;
    d = new Private(*rhs.d);
    delete p;
}


Maildir::Maildir(const Maildir & rhs)
  :d(new Private(*rhs.d))

{
}

Maildir& Maildir::operator= (const Maildir & rhs)
{
    // copy and swap, exception safe, and handles assignment to self
    Maildir temp(rhs);
    swap(temp);
    return *this;
}


bool Maildir::operator== (const Maildir & rhs) const
{
    return *d == *rhs.d;
}


Maildir::~Maildir()
{
    delete d;
}

bool Maildir::Private::canAccess(const QString& path) const
{
    QFileInfo d(path);
    return d.isReadable() && d.isWritable();
}

bool Maildir::Private::accessIsPossible(bool createMissingFolders)
{
    QStringList paths = subPaths();

    paths.prepend(path);

    Q_FOREACH (const QString &p, paths) {
        if (!QFile::exists(p)) {
            if (!createMissingFolders) {
              qCWarning(log) << QString("Error opening %1; this folder is missing.").arg(p);
              return false;
            }
            QDir().mkpath(p);
            if (!QFile::exists(p)) {
              qCWarning(log) << QString("Error opening %1; this folder is missing.").arg(p);
              return false;
            }
        }
        if (!canAccess(p)) {
            qCWarning(log) <<  QString("Error opening %1; either this is not a valid maildir folder, or you do not have sufficient access permissions.").arg(p);
            return false;
        }
    }
    return true;
}

bool Maildir::isValid(bool createMissingFolders) const
{
    if (path().isEmpty()) {
      return false;
    }
    if (!d->isRoot) {
      if (d->accessIsPossible(createMissingFolders)) {
          return true;
      }
    } else {
      Q_FOREACH (const QString &sf, subFolderList()) {
        const Maildir subMd = Maildir(path() + QLatin1Char('/') + sf);
        if (!subMd.isValid()) {
          return false;
        }
      }
      return true;
    }
    return false;
}

bool Maildir::isRoot() const
{
  return d->isRoot;
}

bool Maildir::create()
{
    // FIXME: in a failure case, this will leave partially created dirs around
    // we should clean them up, but only if they didn't previously existed...
    Q_FOREACH (const QString &p, d->subPaths()) {
        QDir dir(p);
        if (!dir.exists(p)) {
            if (!dir.mkpath(p))
                return false;
        }
    }
    return true;
}

bool Maildir::remove()
{
    QDir dir(d->path);
    dir.removeRecursively();
    return true;
}

QString Maildir::path() const
{
    return d->path;
}

QString Maildir::name() const
{
  const QDir dir(d->path);
  return dir.dirName();
}

QString Maildir::addSubFolder(const QString& path)
{
    if (!isValid())
        return QString();

    // make the subdir dir
    QDir dir(d->path);
    if (!d->isRoot) {
        dir.cdUp();
        if (!dir.exists(d->subDirPath()))
            dir.mkdir(d->subDirPath());
        dir.cd(d->subDirPath());
    }

    const QString fullPath = dir.path() + QLatin1Char('/') + path;
    Maildir subdir(fullPath);
    if (subdir.create())
        return fullPath;
    return QString();
}

bool Maildir::removeSubFolder(const QString& folderName)
{
    if (!isValid()) return false;
    QDir dir(d->path);
    if (!d->isRoot) {
        dir.cdUp();
        if (!dir.exists(d->subDirPath())) return false;
        dir.cd(d->subDirPath());
    }
    if (!dir.exists(folderName)) return false;

    // remove it recursively
    bool result = removeDirAndContentsRecursively(dir.absolutePath() + QLatin1Char('/') + folderName);
    QString subfolderName = subDirNameForFolderName(folderName);
    if (dir.exists(subfolderName))
      result &= removeDirAndContentsRecursively(dir.absolutePath() + QLatin1Char('/') + subfolderName);
    return result;
}

Maildir Maildir::subFolder(const QString& subFolder) const
{
    // make the subdir dir
    QDir dir(d->path);
    if (!d->isRoot) {
        dir.cdUp();
        if (dir.exists(d->subDirPath())) {
            dir.cd(d->subDirPath());
        }
    }
    return Maildir(dir.path() + QLatin1Char('/') + subFolder);
}

Maildir Maildir::parent() const
{
  if (!isValid() || d->isRoot)
    return Maildir();
  QDir dir(d->path);
  dir.cdUp();
  //FIXME Figure out how this is acutally supposed to work
  //There seem to be a bunch of conflicting standards, and nesting folders is apparently not what we're supposed to be doing,
  //but it works for the time being.
  // if (!dir.dirName().startsWith(QLatin1Char('.')) || !dir.dirName().endsWith(QLatin1String(".directory")))
  //   return Maildir();
  // const QString parentName = dir.dirName().mid(1, dir.dirName().size() - 11);
  // dir.cdUp();
  // dir.cd(parentName);
  return Maildir (dir.path());
}

QStringList Maildir::entryList() const
{
    QStringList result;
    if (isValid()) {
        result += d->listNew();
        result += d->listCurrent();
    }
    //  qCDebug(log) <<"Maildir::entryList()" << result;
    return result;
}

QStringList Maildir::listCurrent() const
{
    QStringList result;
    if (isValid()) {
        result += d->listCurrent();
    }
    return result;
}

QString Maildir::findRealKey(const QString& key) const
{
    return d->findRealKey(key);
}


QStringList Maildir::listNew() const
{
    QStringList result;
    if (isValid()) {
        result += d->listNew();
    }
    return result;
}

QString Maildir::pathToNew() const
{
    if (isValid()) {
      return d->path + QString::fromLatin1("/new");
    }
    return QString();
}

QString Maildir::pathToCurrent() const
{
    if (isValid()) {
      return d->path + QString::fromLatin1("/cur");
    }
    return QString();
}

QString Maildir::subDirPath() const
{
  QDir dir(d->path);
  dir.cdUp();
  return dir.path() + QDir::separator() + d->subDirPath();
}



QStringList Maildir::subFolderList() const
{
    QDir dir(d->path);

    // the root maildir has its subfolders directly beneath it
    if (!d->isRoot) {
        dir.cdUp();
        if (!dir.exists(d->subDirPath()))
            return QStringList();
        dir.cd(d->subDirPath());
    }
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList entries = dir.entryList();
    entries.removeAll(QLatin1String("cur"));
    entries.removeAll(QLatin1String("new"));
    entries.removeAll(QLatin1String("tmp"));
    return entries;
}

QByteArray Maildir::readEntry(const QString& key) const
{
    QByteArray result;

    QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::readEntry unable to find: " << key;
        return result;
    }

    QFile f(realKey);
    if (!f.open(QIODevice::ReadOnly)) {
      qCWarning(log) << QString("Cannot open mail file %1.").arg(realKey);
      return result;
    }

    result = f.readAll();

    return result;
}
qint64 Maildir::size(const QString& key) const
{
    QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::size unable to find: " << key;
        return -1;
    }

    QFileInfo info(realKey);
    if (!info.exists()) {
        qCWarning(log) << "Cannot open mail file:" << realKey;
        return -1;
    }

    return info.size();
}

QDateTime Maildir::lastModified(const QString& key) const
{
    const QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::lastModified unable to find: " << key;
        return QDateTime();
    }

    const QFileInfo info(realKey);
    if (!info.exists())
        return QDateTime();

    return info.lastModified();
}

void Maildir::importNewMails()
{
    QDirIterator entryIterator(pathToNew(), QDir::Files);
    while (entryIterator.hasNext()) {
        const QString filePath = QDir::fromNativeSeparators(entryIterator.next());
        QFile file(filePath);
        if (!file.rename(pathToCurrent() +"/" + entryIterator.fileName())) {
            qCWarning(log) << "Failed to rename the file: " << file.errorString();
        }
    }
}

QString Maildir::getKeyFromFile(const QString& file)
{
    return Maildir::Private::stripFlags(file.split('/').last());
}

QString Maildir::getDirectoryFromFile( const QString& file )
{
    auto parts = file.split('/');
    Q_ASSERT(parts.size() >= 2);
    parts.removeLast(); //File
    parts.removeLast(); //cur/new/tmp
    return parts.join('/') + "/";
}

QByteArray Maildir::readEntryHeadersFromFile(const QString& file)
{
    QByteArray result;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(log) << "Maildir::readEntryHeaders unable to find: " << file;
        return result;
    }
    f.map(0, qMin((qint64)8000, f.size()));
    forever {
        QByteArray line = f.readLine();
        if (line.isEmpty() || line.startsWith('\n'))
            break;
        result.append(line);
    }
    return result;
}

QByteArray Maildir::readEntryHeaders(const QString& key) const
{
    const QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::readEntryHeaders unable to find: " << key;
        return QByteArray();
    }

  return readEntryHeadersFromFile(realKey);
}


static QString createUniqueFileName()
{
    qint64 time = QDateTime::currentMSecsSinceEpoch() / 1000;
    int r = qrand() % 1000;
    QString identifier = QLatin1String("R") + QString::number(r);

    QString fileName = QString::number(time) + QLatin1Char('.') + identifier + QLatin1Char('.');

    return fileName;
}

bool Maildir::writeEntry(const QString& key, const QByteArray& data)
{
    QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::writeEntry unable to find: " << key;
        return false;
    }
    QFile f(realKey);
    bool result = f.open(QIODevice::WriteOnly);
    result = result & (f.write(data) != -1);
    f.close();
    if (!result) {
       qCWarning(log) << "Cannot write to mail file %1." << realKey;
       return false;
    }
    return true;
}

QString Maildir::addEntry(const QByteArray& data)
{
    QString uniqueKey;
    QString key;
    QString finalKey;
    QString curKey;

    // QUuid doesn't return globally unique identifiers, therefor we query until we
    // get one that doesn't exists yet
    do {
      uniqueKey = createUniqueFileName() + d->hostName;
      key = d->path + QLatin1String("/tmp/") + uniqueKey;
      finalKey = d->path + QLatin1String("/cur/") + uniqueKey;
      curKey = d->path + QLatin1String("/cur/") + uniqueKey;
    } while (QFile::exists(key) || QFile::exists(finalKey) || QFile::exists(curKey));

    QFile f(key);
    bool result = f.open(QIODevice::WriteOnly);
    if (!result) {
       qCWarning(log) << f.errorString();
       qCWarning(log) << "Cannot write to mail file: " << key;
    }
    result = result & (f.write(data) != -1);
    f.close();
    if (!result) {
       qCWarning(log) << "Cannot write to mail file: " << key;
       return QString();
    }
    /*
     * FIXME:
     *
     * The whole point of the locking free maildir idea is that the moves between
     * the internal directories are atomic. Afaik QFile::rename does not guarantee
     * that, so this will need to be done properly. - ta
     *
     * For reference: http://trolltech.com/developer/task-tracker/index_html?method=entry&id=211215
     */
    qCDebug(log) << "New entry: " << finalKey;
    if (!f.rename(finalKey)) {
        qCWarning(log) << "Maildir: Failed to add entry: " << finalKey  << "! Error: " << f.errorString();
        return QString();
    }
    return uniqueKey;
}

QString Maildir::addEntryFromPath(const QString& path)
{
    QString uniqueKey;
    QString key;
    QString finalKey;
    QString curKey;

    // QUuid doesn't return globally unique identifiers, therefor we query until we
    // get one that doesn't exists yet
    do {
      uniqueKey = createUniqueFileName() + d->hostName;
      key = d->path + QLatin1String("/tmp/") + uniqueKey;
      finalKey = d->path + QLatin1String("/new/") + uniqueKey;
      curKey = d->path + QLatin1String("/cur/") + uniqueKey;
    } while (QFile::exists(key) || QFile::exists(finalKey) || QFile::exists(curKey));

    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) {
       qCWarning(log) << f.errorString();
       qCWarning(log) << "Cannot open mail file: " << key;
       return QString();
    }

    if (!f.rename(curKey)) {
        qCWarning(log) << "Maildir: Failed to add entry: " << curKey  << "! Error: " << f.errorString();
        return QString();
    }
    return uniqueKey;
}

bool Maildir::removeEntry(const QString& key)
{
    QString realKey(d->findRealKey(key));
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::removeEntry unable to find: " << key;
        return false;
    }
    QFile file(realKey);
    if (!file.remove()) {
        qCWarning(log) << file.errorString() << file.error();
        return false;
    }
    return true;
}

QString Maildir::changeEntryFlags(const QString& key, const Maildir::Flags& flags)
{
    QString realKey(d->findRealKey(key));
    qCDebug(log) << "Change entry flags: " << key << realKey;
    if (realKey.isEmpty()) {
        qCWarning(log) << "Maildir::changeEntryFlags unable to find: " << key << "in " << d->path;
        return QString();
    }

    const QRegExp rx = *(statusSeparatorRx());
    QString finalKey = key.left(key.indexOf(rx));

    QStringList mailDirFlags;
    if (flags & Forwarded)
        mailDirFlags << QLatin1String("P");
    if (flags & Replied)
        mailDirFlags << QLatin1String("R");
    if (flags & Seen)
        mailDirFlags << QLatin1String("S");
    if (flags & Deleted)
        mailDirFlags << QLatin1String("T");
    if (flags & Flagged)
        mailDirFlags << QLatin1String("F");

    mailDirFlags.sort();
    if (!mailDirFlags.isEmpty()) {
#ifdef Q_OS_WIN
      finalKey.append(QLatin1String("!2,") + mailDirFlags.join(QString()));
#else
      finalKey.append(QLatin1String(":2,") + mailDirFlags.join(QString()));
#endif
    }

    QString newUniqueKey = finalKey; //key without path
    finalKey.prepend(d->path + QString::fromLatin1("/cur/"));

    if (realKey == finalKey) {
      // Somehow it already is named this way (e.g. migration bug -> wrong status in sink)
      // We run into this if we pick up flag changes from the source and call this method with unchanged flags.
      qCDebug(log) << "File already named that way: " << newUniqueKey << finalKey;
      return newUniqueKey;
    }

    QFile f(realKey);
    if (QFile::exists(finalKey)) {
      QFile destFile(finalKey);
      QByteArray destContent;
      if (destFile.open(QIODevice::ReadOnly)) {
        destContent = destFile.readAll();
        destFile.close();
      }
      QByteArray sourceContent;
      if (f.open(QIODevice::ReadOnly)) {
        sourceContent = f.readAll();
        f.close();
      }

      if (destContent != sourceContent) {
         QString newFinalKey = QLatin1String("1-") + newUniqueKey;
         int i = 1;
         while (QFile::exists(d->path + QString::fromLatin1("/cur/") + newFinalKey)) {
           i++;
           newFinalKey = QString::number(i) + QLatin1Char('-') + newUniqueKey;
         }
         finalKey = d->path + QString::fromLatin1("/cur/") + newFinalKey;
      } else {
            QFile::remove(finalKey); //they are the same
      }
    }

    if (!f.rename(finalKey)) {
        qCWarning(log) << "Maildir: Failed to rename entry from: " << f.fileName() << " to "  << finalKey  << "! Error: " << f.errorString();
        return QString();
    }
    qCDebug(log) << "Renamed file from: " << f.fileName() << " to " << finalKey;

    return newUniqueKey;
}

Maildir::Flags Maildir::readEntryFlags(const QString& key)
{
    Flags flags;
    const QRegExp rx = *(statusSeparatorRx());
    const int index = key.indexOf(rx);
    if (index != -1) {
        const QString mailDirFlags = key.mid(index + 3); // after "(:|!)2,"
        const int flagSize(mailDirFlags.size());
        for (int i = 0; i < flagSize; ++i) {
            if (mailDirFlags[i] == QLatin1Char('P'))
                flags |= Forwarded;
            else if (mailDirFlags[i] == QLatin1Char('R'))
                flags |= Replied;
            else if (mailDirFlags[i] == QLatin1Char('S'))
                flags |= Seen;
            else if (mailDirFlags[i] == QLatin1Char('F'))
                flags |= Flagged;
        }
    }

    return flags;
}

bool Maildir::moveTo(const Maildir &newParent)
{
  if (d->isRoot)
    return false; // not supported

  QDir newDir(newParent.path());
  if (!newParent.d->isRoot) {
    newDir.cdUp();
    if (!newDir.exists(newParent.d->subDirPath()))
      newDir.mkdir(newParent.d->subDirPath());
    newDir.cd(newParent.d->subDirPath());
  }

  QDir currentDir(d->path);
  currentDir.cdUp();

  if (newDir == currentDir)
    return true;

  return d->moveAndRename(newDir, name());
}

bool Maildir::rename(const QString &newName)
{
  if (name() == newName)
    return true;
  if (d->isRoot)
    return false; // not (yet) supported

  QDir dir(d->path);
  dir.cdUp();

  return d->moveAndRename(dir, newName);
}

QString Maildir::moveEntryTo(const QString &key, const Maildir &destination)
{
  const QString realKey(d->findRealKey(key));
  if (realKey.isEmpty()) {
    qCWarning(log) << "Unable to find: " << key;
    return QString();
  }
  QFile f(realKey);
  // ### is this safe regarding the maildir locking scheme?
  const QString targetKey = destination.path() + QDir::separator() + QLatin1String("cur") + QDir::separator() + key;
  if (!f.rename(targetKey)) {
    qCWarning(log) << "Failed to rename" << realKey << "to" << targetKey << "! Error: " << f.errorString();;
    return QString();
  }

  return key;
}

QString Maildir::subDirPathForFolderPath(const QString &folderPath)
{
  QDir dir(folderPath);
  const QString dirName = dir.dirName();
  dir.cdUp();
  return QFileInfo(dir, Private::subDirNameForFolderName(dirName)).filePath();
}

QString Maildir::subDirNameForFolderName(const QString &folderName)
{
  return Private::subDirNameForFolderName(folderName);
}

