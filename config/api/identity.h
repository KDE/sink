#pragma once

#include <QObject>
#include <QString>

class Identity : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signature READ signature WRITE setSignature NOTIFY signatureChanged)

public:
    explicit Identity(QObject *parent = 0);

    QString signature() const;
    void setSignature(const QString &signature);

signals:
    void signatureChanged();

public slots:
    void loadConfig(const QString &id);
    void saveConfig();

private:
    QString m_id;
    QString m_signature;
};