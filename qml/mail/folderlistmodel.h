#pragma once

#include <QAbstractListModel>
#include <QStringList>

class FolderListModel : public QAbstractListModel
{
    Q_OBJECT
    
public:
    FolderListModel(QObject *parent = Q_NULLPTR);
    ~FolderListModel();
    
    enum Roles {
        FolderName  = Qt::UserRole + 1,
        IconName
    };
    
    QHash<int, QByteArray> roleNames() const;
    QVariant data(const QModelIndex &index, int role) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    
    bool addFolders(const QStringList &folders);
    void clearFolders();
    
private:
    QStringList m_folders;
};
