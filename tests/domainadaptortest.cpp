#include <QTest>

#include <QString>
#include <QSharedPointer>
#include <QDebug>

#include "dummyresource/resourcefactory.h"
#include "store.h"

#include "common/domainadaptor.h"
#include "common/entitybuffer.h"
#include "event_generated.h"
#include "mail_generated.h"
#include "metadata_generated.h"
#include "entity_generated.h"

class TestFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Event>
{
public:
    TestFactory() = default;
};

class TestMailFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Mail>
{
public:
    TestMailFactory() = default;
};

class TestContactFactory : public DomainTypeAdaptorFactory<Sink::ApplicationDomain::Contact>
{
public:
    TestContactFactory() = default;
};

/**
 * Test of domain adaptor, that it can read and write buffers.
 */
class DomainAdaptorTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
    }

    void cleanupTestCase()
    {
    }

    void testCreateBufferPart()
    {
        auto writeMapper = QSharedPointer<PropertyMapper>::create();
        Sink::ApplicationDomain::TypeImplementation<Sink::ApplicationDomain::Event>::configure(*writeMapper);

        Sink::ApplicationDomain::Event event;
        event.setProperty("summary", "foo");

        flatbuffers::FlatBufferBuilder fbb;
        auto pos = createBufferPart<Sink::ApplicationDomain::Buffer::EventBuilder, Sink::ApplicationDomain::Buffer::Event>(event, fbb, *writeMapper);
        Sink::ApplicationDomain::Buffer::FinishEventBuffer(fbb, pos);

        flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
        QVERIFY(verifier.VerifyBuffer<Sink::ApplicationDomain::Buffer::Event>(nullptr));
    }

    void testAdaptor()
    {
        // Create entity buffer
        flatbuffers::FlatBufferBuilder metadataFbb;
        auto metadataBuilder = Sink::MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(1);
        auto metadataBuffer = metadataBuilder.Finish();
        Sink::FinishMetadataBuffer(metadataFbb, metadataBuffer);

        flatbuffers::FlatBufferBuilder m_fbb;
        auto summary = m_fbb.CreateString("summary1");
        auto description = m_fbb.CreateString("description");

        auto builder = Sink::ApplicationDomain::Buffer::EventBuilder(m_fbb);
        builder.add_summary(summary);
        builder.add_description(description);
        auto buffer = builder.Finish();
        Sink::ApplicationDomain::Buffer::FinishEventBuffer(m_fbb, buffer);

        flatbuffers::FlatBufferBuilder fbb;
        Sink::EntityBuffer::assembleEntityBuffer(
            fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), m_fbb.GetBufferPointer(), m_fbb.GetSize(), m_fbb.GetBufferPointer(), m_fbb.GetSize());

        // Extract entity buffer
        {
            std::string data(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
            Sink::EntityBuffer buffer((void *)(data.data()), data.size());

            TestFactory factory;
            auto adaptor = factory.createAdaptor(buffer.entity());
            QCOMPARE(adaptor->getProperty("summary").toString(), QString("summary1"));
        }
    }

    void testMail()
    {
        auto writeMapper = QSharedPointer<PropertyMapper>::create();
        Sink::ApplicationDomain::TypeImplementation<Sink::ApplicationDomain::Mail>::configure(*writeMapper);

        Sink::ApplicationDomain::Mail mail;
        mail.setExtractedSubject("summary");
        mail.setMimeMessage("foobar");
        mail.setFolder("folder");

        flatbuffers::FlatBufferBuilder metadataFbb;
        auto metadataBuilder = Sink::MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(1);
        auto metadataBuffer = metadataBuilder.Finish();
        Sink::FinishMetadataBuffer(metadataFbb, metadataBuffer);

        flatbuffers::FlatBufferBuilder mailFbb;
        auto pos = createBufferPart<Sink::ApplicationDomain::Buffer::MailBuilder, Sink::ApplicationDomain::Buffer::Mail>(mail, mailFbb, *writeMapper);
        Sink::ApplicationDomain::Buffer::FinishMailBuffer(mailFbb, pos);

        flatbuffers::FlatBufferBuilder fbb;
        Sink::EntityBuffer::assembleEntityBuffer(
            fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), mailFbb.GetBufferPointer(), mailFbb.GetSize(), mailFbb.GetBufferPointer(), mailFbb.GetSize());

        {
            std::string data(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
            Sink::EntityBuffer buffer((void *)(data.data()), data.size());

            TestMailFactory factory;
            auto adaptor = factory.createAdaptor(buffer.entity());
            Sink::ApplicationDomain::Mail readMail{QByteArray{}, QByteArray{}, 0, adaptor};
            QCOMPARE(readMail.getSubject(), mail.getSubject());
            QCOMPARE(readMail.getMimeMessage(), mail.getMimeMessage());
            QCOMPARE(readMail.getFolder(), mail.getFolder());
        }

    }

    void testContact()
    {
        auto writeMapper = QSharedPointer<PropertyMapper>::create();
        Sink::ApplicationDomain::TypeImplementation<Sink::ApplicationDomain::Contact>::configure(*writeMapper);

        auto binaryData = QByteArray::fromRawData("\xEF\xBF\xBD\x00\xEF\xBF", 5);

        Sink::ApplicationDomain::Contact contact;
        contact.setPhoto(binaryData);
        QVERIFY(!contact.getPhoto().isEmpty());

        flatbuffers::FlatBufferBuilder metadataFbb;
        auto metadataBuilder = Sink::MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(1);
        auto metadataBuffer = metadataBuilder.Finish();
        Sink::FinishMetadataBuffer(metadataFbb, metadataBuffer);

        flatbuffers::FlatBufferBuilder mailFbb;
        auto pos = createBufferPart<Sink::ApplicationDomain::Buffer::ContactBuilder, Sink::ApplicationDomain::Buffer::Contact>(contact, mailFbb, *writeMapper);
        Sink::ApplicationDomain::Buffer::FinishContactBuffer(mailFbb, pos);

        flatbuffers::FlatBufferBuilder fbb;
        Sink::EntityBuffer::assembleEntityBuffer(
            fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), mailFbb.GetBufferPointer(), mailFbb.GetSize(), mailFbb.GetBufferPointer(), mailFbb.GetSize());

        {
            std::string data(reinterpret_cast<const char *>(fbb.GetBufferPointer()), fbb.GetSize());
            Sink::EntityBuffer buffer((void *)(data.data()), data.size());

            TestContactFactory factory;
            auto adaptor = factory.createAdaptor(buffer.entity());
            Sink::ApplicationDomain::Contact readContact{QByteArray{}, QByteArray{}, 0, adaptor};
            QCOMPARE(readContact.getPhoto(), contact.getPhoto());
        }

    }
};

QTEST_MAIN(DomainAdaptorTest)
#include "domainadaptortest.moc"
