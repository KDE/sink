/*
    Copyright (c) 2022 Christian Mollekopf <christian@mkpf.ch>

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
#include <objecttreeparser.h>

#include <QTest>
#include <QDebug>

QByteArray readMailFromFile(const QString &mailFile)
{
    QFile file(QLatin1String(MAIL_DATA_DIR) + QLatin1Char('/') + mailFile);
    file.open(QIODevice::ReadOnly);
    Q_ASSERT(file.isOpen());
    return file.readAll();
}

class MimeTreeParserBenchmark : public QObject
{
    Q_OBJECT
private slots:
    void testOpenPGPInlineBenchmark()
    {
        MimeTreeParser::ObjectTreeParser otp;
        otp.parseObjectTree(readMailFromFile("openpgp-inline-charset-encrypted.mbox"));
        otp.print();
        QElapsedTimer timer;
        timer.start();
        otp.decryptParts();

        qWarning() << "Decryption took: " << timer.elapsed();

        QBENCHMARK {
            otp.decryptParts();
        }

    }
};

QTEST_GUILESS_MAIN(MimeTreeParserBenchmark)
#include "mimetreeparserbenchmark.moc"
