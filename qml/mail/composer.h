#pragma once

#include <QObject>
#include <QString>

class Composer : public QObject
{
    Q_OBJECT

    Q_PROPERTY (QString replyTo READ replyTo WRITE setReplyTo NOTIFY replyToChanged)
    //TODO
    /*
     * replyToAll
     * forward
     */
    Q_PROPERTY (QString subject READ subject WRITE setSubject NOTIFY subjectChanged)
    Q_PROPERTY (QString body READ body WRITE setBody NOTIFY bodyChanged)
    Q_PROPERTY (QString to READ to WRITE setTo NOTIFY toChanged)

public:
    explicit Composer( QObject *parent = 0 );

    QString replyTo() const;
    QString to() const;
    QString subject() const;
    QString body() const;

    void setReplyTo( const QString &replyTo);
    void setTo( const QString &to );
    void setSubject( const QString &subject );
    void setBody ( const QString &body );

signals:
    void replyToChanged();
    void subjectChanged();
    void bodyChanged();
    void toChanged();
    void messageSend();

public slots:
    void send();

private:
    QString m_replyTo;
    QString m_to;
    QString m_subject;
    QString m_body;
};