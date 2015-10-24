#include <QtTest>

#include <QString>
#include <QSharedPointer>
#include <QDebug>

#include "dummyresource/resourcefactory.h"
#include "clientapi.h"

#include "common/domainadaptor.h"
#include "common/entitybuffer.h"
#include "event_generated.h"
#include "metadata_generated.h"
#include "entity_generated.h"

class TestFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::EventBuilder>
{
public:
    TestFactory()
    {
        mResourceWriteMapper = Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::initializeWritePropertyMapper();
    }
};

/**
 * Test of domain adaptor, that it can read and write buffers.
 */
class DomainAdaptorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
    }

    void cleanupTestCase()
    {
    }

    void testCreateBufferPart()
    {
        auto writeMapper = Akonadi2::ApplicationDomain::TypeImplementation<Akonadi2::ApplicationDomain::Event>::initializeWritePropertyMapper();

        Akonadi2::ApplicationDomain::Event event;
        event.setProperty("summary", "foo");

        flatbuffers::FlatBufferBuilder fbb;
        auto pos = createBufferPart<Akonadi2::ApplicationDomain::Buffer::EventBuilder, Akonadi2::ApplicationDomain::Buffer::Event>(event, fbb, *writeMapper);
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(fbb, pos);

        flatbuffers::Verifier verifier(fbb.GetBufferPointer(), fbb.GetSize());
        QVERIFY(verifier.VerifyBuffer<Akonadi2::ApplicationDomain::Buffer::Event>());
    }

    void testAdaptor()
    {
        //Create entity buffer
        flatbuffers::FlatBufferBuilder metadataFbb;
        auto metadataBuilder = Akonadi2::MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(1);
        metadataBuilder.add_processed(false);
        auto metadataBuffer = metadataBuilder.Finish();
        Akonadi2::FinishMetadataBuffer(metadataFbb, metadataBuffer);

        flatbuffers::FlatBufferBuilder m_fbb;
        auto summary = m_fbb.CreateString("summary1");
        auto description = m_fbb.CreateString("description");
        static uint8_t rawData[100];
        auto attachment = m_fbb.CreateVector(rawData, 100);

        auto builder = Akonadi2::ApplicationDomain::Buffer::EventBuilder(m_fbb);
        builder.add_summary(summary);
        builder.add_description(description);
        builder.add_attachment(attachment);
        auto buffer = builder.Finish();
        Akonadi2::ApplicationDomain::Buffer::FinishEventBuffer(m_fbb, buffer);

        flatbuffers::FlatBufferBuilder fbb;
        Akonadi2::EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), m_fbb.GetBufferPointer(), m_fbb.GetSize(), m_fbb.GetBufferPointer(), m_fbb.GetSize());

        //Extract entity buffer
        {
            std::string data(reinterpret_cast<const char*>(fbb.GetBufferPointer()), fbb.GetSize());
            Akonadi2::EntityBuffer buffer((void*)(data.data()), data.size());

            TestFactory factory;
            auto adaptor = factory.createAdaptor(buffer.entity());
            QCOMPARE(adaptor->getProperty("summary").toString(), QString("summary1"));
        }
    }

};

QTEST_MAIN(DomainAdaptorTest)
#include "domainadaptortest.moc"
