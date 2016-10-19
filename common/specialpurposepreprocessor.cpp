#include "specialpurposepreprocessor.h"
#include "query.h"
#include "applicationdomaintype.h"

using namespace Sink;

SINK_DEBUG_AREA("SpecialPurposeProcessor")

static QHash<QByteArray, QString> specialPurposeFolders()
{
    QHash<QByteArray, QString> hash;
        //FIXME localize
    //TODO inbox
    //TODO use standardized values
    hash.insert("drafts", "Drafts");
    hash.insert("trash", "Trash");
    hash.insert("inbox", "Inbox");
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
    /* if (!mSpecialPurposeFolders.contains(specialPurpose)) { */
    /*     //Try to find an existing drafts folder */
    /*     Sink::EntityReader<ApplicationDomain::Folder> reader(mResourceType, mResourceInstanceIdentifier); */
    /*     reader.query(Sink::Query().filter<ApplicationDomain::Folder::SpecialPurpose>(Query::Comparator(specialPurpose, Query::Comparator::Contains)), */
    /*         [this, specialPurpose](const ApplicationDomain::Folder &f) -> bool{ */
    /*             mSpecialPurposeFolders.insert(specialPurpose, f.identifier()); */
    /*             return false; */
    /*         }); */
    /*     if (!mSpecialPurposeFolders.contains(specialPurpose)) { */
    /*         SinkTrace() << "Failed to find a drafts folder, creating a new one"; */
    /*         auto folder = ApplicationDomain::Folder::create(mResourceInstanceIdentifier); */
    /*         folder.setSpecialPurpose(QByteArrayList() << specialPurpose); */
    /*         folder.setName(sSpecialPurposeFolders.value(specialPurpose)); */
    /*         folder.setIcon("folder"); */
    /*         //This processes the pipeline synchronously */
    /*         createEntity(folder); */
    /*         mSpecialPurposeFolders.insert(specialPurpose, folder.identifier()); */
    /*     } */
    /* } */
    return mSpecialPurposeFolders.value(specialPurpose);
}

void SpecialPurposeProcessor::moveToFolder(Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    if (newEntity.getProperty("trash").toBool()) {
        newEntity.setProperty("folder", ensureFolder("trash"));
        return;
    }
    if (newEntity.getProperty("draft").toBool()) {
        newEntity.setProperty("folder", ensureFolder("drafts"));
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
