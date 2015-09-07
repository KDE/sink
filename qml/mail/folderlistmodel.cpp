#include "folderlistmodel.h"

FolderListModel::FolderListModel(QObject* parent) : QAbstractListModel(parent)
{

}

FolderListModel::~FolderListModel()
{

}

QVariant FolderListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    
    if (index.row() >= m_folders.count() || index.row() < 0) {
        return QVariant();
    }
    
        switch (role) {
            case FolderName:
                return "TODO";
            case IconName:
                return "TODO";
        }
    return QVariant();
}

QHash<int, QByteArray> FolderListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[FolderName] = "folderName";
    roles[IconName] = "iconName";
    return roles;
}

int FolderListModel::rowCount(const QModelIndex& parent) const
{
    return m_folders.size();
}

bool FolderListModel::addFolders(const QStringList& folders)
{
    beginInsertRows(QModelIndex(), rowCount(), rowCount() + folders.size() - 1);
    
    foreach (const QString &item, folders) {
        m_folders.append(item);
    }
    
    endInsertRows();
    
    return true;
}


void FolderListModel::clearFolders()
{
    if (!m_folders.isEmpty()) {
        beginResetModel();
        m_folders.clear();
        endResetModel();
    }
}