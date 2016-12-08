#include "specialpurposepreprocessor.h"
#include "query.h"
#include "applicationdomaintype.h"
#include "datastorequery.h"

using namespace Sink;

SINK_DEBUG_AREA("SpecialPurposeProcessor")

static QHash<QByteArray, QString> specialPurposeFolders()
{
    QHash<QByteArray, QString> hash;
        //FIXME localize
    //TODO inbox
    //TODO use standardized values
    hash.insert(ApplicationDomain::SpecialPurpose::Mail::drafts, "Drafts");
    hash.insert(ApplicationDomain::SpecialPurpose::Mail::trash, "Trash");
    hash.insert(ApplicationDomain::SpecialPurpose::Mail::inbox, "Inbox");
    hash.insert(ApplicationDomain::SpecialPurpose::Mail::sent, "Sent");
    return hash;
}

static QHash<QString, QByteArray> specialPurposeNames()
{
    QHash<QString, QByteArray> hash;
    for (const auto &value : specialPurposeFolders().values()) {
        hash.insert(value.toLower(), specialPurposeFolders().key(value));
    }
    return hash;
}

//specialpurpose, name
static QHash<QByteArray, QString> sSpecialPurposeFolders = specialPurposeFolders();
//Lowercase-name, specialpurpose
static QHash<QString, QByteArray> sSpecialPurposeNames = specialPurposeNames();

namespace SpecialPurpose {
bool isSpecialPurposeFolderName(const QString &name)
{
    return sSpecialPurposeNames.contains(name.toLower());
}

QByteArray getSpecialPurposeType(const QString &name)
{
    return sSpecialPurposeNames.value(name.toLower());
}
}

SpecialPurposeProcessor::SpecialPurposeProcessor(const QByteArray &resourceType, const QByteArray &resourceInstanceIdentifier) : mResourceType(resourceType), mResourceInstanceIdentifier(resourceInstanceIdentifier) {}

QByteArray SpecialPurposeProcessor::ensureFolder(const QByteArray &specialPurpose)
{
    if (!mSpecialPurposeFolders.contains(specialPurpose)) {
        //Try to find an existing drafts folder
        DataStoreQuery dataStoreQuery{Sink::Query().filter<ApplicationDomain::Folder::SpecialPurpose>(Query::Comparator(specialPurpose, Query::Comparator::Contains)), ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), entityStore()};
        auto resultSet = dataStoreQuery.execute();
        resultSet.replaySet(0, 1, [&, this](const ResultSet::Result &r) {
            mSpecialPurposeFolders.insert(specialPurpose, r.entity.identifier());
        });

        if (!mSpecialPurposeFolders.contains(specialPurpose)) {
            SinkTrace() << "Failed to find a " << specialPurpose << " folder, creating a new one";
            auto folder = ApplicationDomain::Folder::create(mResourceInstanceIdentifier);
            folder.setSpecialPurpose(QByteArrayList() << specialPurpose);
            folder.setName(sSpecialPurposeFolders.value(specialPurpose));
            folder.setIcon("folder");
            //This processes the pipeline synchronously
            createEntity(folder);
            mSpecialPurposeFolders.insert(specialPurpose, folder.identifier());
        }
    }
    return mSpecialPurposeFolders.value(specialPurpose);
}

void SpecialPurposeProcessor::moveToFolder(Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    using namespace Sink::ApplicationDomain;
    auto mail = newEntity.cast<Mail>();
    if (mail.getTrash()) {
        auto f = ensureFolder(ApplicationDomain::SpecialPurpose::Mail::trash);
        SinkTrace() << "Setting trash folder: " << f;
        mail.setFolder(f);
        return;
    }
    if (mail.getDraft()) {
        mail.setFolder(ensureFolder(ApplicationDomain::SpecialPurpose::Mail::drafts));
        return;
    }
    if (mail.getSent()) {
        mail.setFolder(ensureFolder(ApplicationDomain::SpecialPurpose::Mail::sent));
        return;
    }
}

void SpecialPurposeProcessor::newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    moveToFolder(newEntity);
}

void SpecialPurposeProcessor::modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    moveToFolder(newEntity);
}
