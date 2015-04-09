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

class TestEventAdaptor : public Akonadi2::ApplicationDomain::BufferAdaptor
{
public:
    TestEventAdaptor()
        : Akonadi2::ApplicationDomain::BufferAdaptor()
    {
    }

    void setProperty(const QByteArray &key, const QVariant &value)
    {
        if (mResourceMapper->mWriteAccessors.contains(key)) {
            // mResourceMapper.setProperty(key, value, mResourceBuffer);
        } else {
            // mLocalMapper.;
        }
    }

    virtual QVariant getProperty(const QByteArray &key) const
    {
        if (mResourceBuffer && mResourceMapper->mReadAccessors.contains(key)) {
            return mResourceMapper->getProperty(key, mResourceBuffer);
        } else if (mLocalBuffer) {
            return mLocalMapper->getProperty(key, mLocalBuffer);
        }
        return QVariant();
    }

    Akonadi2::ApplicationDomain::Buffer::Event const *mLocalBuffer;
    Akonadi2::ApplicationDomain::Buffer::Event const *mResourceBuffer;

    QSharedPointer<PropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> > mLocalMapper;
    QSharedPointer<PropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> > mResourceMapper;
};

class TestFactory : public DomainTypeAdaptorFactory<Akonadi2::ApplicationDomain::Event, Akonadi2::ApplicationDomain::Buffer::Event, Akonadi2::ApplicationDomain::Buffer::Event>
{
public:
    TestFactory()
    {
        mResourceMapper = QSharedPointer<PropertyMapper<Akonadi2::ApplicationDomain::Buffer::Event> >::create();
        mResourceMapper->mReadAccessors.insert("summary", [](Akonadi2::ApplicationDomain::Buffer::Event const *buffer) -> QVariant {
            if (buffer->summary()) {
                return QString::fromStdString(buffer->summary()->c_str());
            }
            return QVariant();
        });
    }

    virtual QSharedPointer<Akonadi2::ApplicationDomain::BufferAdaptor> createAdaptor(const Akonadi2::Entity &entity)
    {
        Akonadi2::ApplicationDomain::Buffer::Event const *resourceBuffer = 0;
        if (auto resourceData = entity.resource()) {
            flatbuffers::Verifier verifyer(resourceData->Data(), resourceData->size());
            if (Akonadi2::ApplicationDomain::Buffer::VerifyEventBuffer(verifyer)) {
                resourceBuffer = Akonadi2::ApplicationDomain::Buffer::GetEvent(resourceData->Data());
                if (resourceBuffer->summary()) {
                    qDebug() << QString::fromStdString(std::string(resourceBuffer->summary()->c_str()));
                }
            }
        }

        // Akonadi2::Metadata const *metadataBuffer = 0;
        // if (auto metadataData = entity.metadata()) {
        //     flatbuffers::Verifier verifyer(metadataData->Data(), metadataData->size());
        //     if (Akonadi2::VerifyMetadataBuffer(verifyer)) {
        //         metadataBuffer = Akonadi2::GetMetadata(metadataData);
        //     }
        // }

        Akonadi2::ApplicationDomain::Buffer::Event const *localBuffer = 0;
        if (auto localData = entity.local()) {
            flatbuffers::Verifier verifyer(localData->Data(), localData->size());
            if (Akonadi2::ApplicationDomain::Buffer::VerifyEventBuffer(verifyer)) {
                localBuffer = Akonadi2::ApplicationDomain::Buffer::GetEvent(localData);
            }
        }

        auto adaptor = QSharedPointer<TestEventAdaptor>::create();
        adaptor->mLocalBuffer = localBuffer;
        adaptor->mResourceBuffer = resourceBuffer;
        adaptor->mResourceMapper = mResourceMapper;
        adaptor->mLocalMapper = mLocalMapper;
        return adaptor;
    }
};

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
