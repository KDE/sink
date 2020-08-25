#include "specialpurposepreprocessor.h"
#include "query.h"
#include "applicationdomaintype.h"
#include "datastorequery.h"

using namespace Sink;

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

SpecialPurposeProcessor::SpecialPurposeProcessor() : Sink::Preprocessor() {}

QByteArray SpecialPurposeProcessor::findFolder(const QByteArray &specialPurpose, bool createIfMissing)
{
    if (!mSpecialPurposeFolders.contains(specialPurpose)) {
        //Try to find an existing and enabled folder we can use.
        auto query = Sink::Query();
        query.filter<ApplicationDomain::Folder::SpecialPurpose>(Query::Comparator(specialPurpose, Query::Comparator::Contains))
        query.filter<ApplicationDomain::Folder::Enabled>(true);
        query.request<ApplicationDomain::Folder::Enabled>();

        DataStoreQuery dataStoreQuery{query, ApplicationDomain::getTypeName<ApplicationDomain::Folder>(), entityStore()};
        auto resultSet = dataStoreQuery.execute();
        resultSet.replaySet(0, 1, [&, this](const ResultSet::Result &r) {
            mSpecialPurposeFolders.insert(specialPurpose, r.entity.identifier());
        });

        //Create a new folder if we couldn't find one. We will also create a new folder if the old one was disabled for some reason.
        if (!mSpecialPurposeFolders.contains(specialPurpose) && createIfMissing) {
            SinkTrace() << "Failed to find a " << specialPurpose << " folder, creating a new one";
            auto folder = ApplicationDomain::Folder::create(resourceInstanceIdentifier());
            folder.setSpecialPurpose(QByteArrayList() << specialPurpose);
            folder.setName(sSpecialPurposeFolders.value(specialPurpose));
            folder.setIcon("folder");
            folder.setEnabled(true);
            //This processes the pipeline synchronously
            createEntity(folder);
            mSpecialPurposeFolders.insert(specialPurpose, folder.identifier());
        }
    }
    return mSpecialPurposeFolders.value(specialPurpose);
}

bool SpecialPurposeProcessor::isSpecialPurposeFolder(const QByteArray &folder) const
{
    return mSpecialPurposeFolders.values().contains(folder);
}

void SpecialPurposeProcessor::moveToFolder(Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    //If we remove the draft folder move back to inbox
    //If we remove the trash property, move back to other specialpurpose folder or inbox
    //If a folder is set explicitly, clear specialpurpose flags.
    using namespace Sink::ApplicationDomain;
    auto mail = newEntity.cast<Mail>();
    if (mail.getTrash()) {
        auto f = findFolder(ApplicationDomain::SpecialPurpose::Mail::trash, true);
        SinkTrace() << "Setting trash folder: " << f;
        mail.setFolder(f);
    } else if (mail.getDraft()) {
        SinkTrace() << "Setting drafts folder: ";
        mail.setFolder(findFolder(ApplicationDomain::SpecialPurpose::Mail::drafts, true));
    } else if (mail.getSent()) {
        SinkTrace() << "Setting sent folder: ";
        mail.setFolder(findFolder(ApplicationDomain::SpecialPurpose::Mail::sent, true));
    } else {
        //No longer a specialpurpose mail, so move to inbox
        if (isSpecialPurposeFolder(mail.getFolder()) || mail.getFolder().isEmpty()) {
            mail.setFolder(findFolder(ApplicationDomain::SpecialPurpose::Mail::inbox, true));
        }
    }
}

void SpecialPurposeProcessor::newEntity(Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    auto mail = newEntity.cast<ApplicationDomain::Mail>();
    const auto folder = mail.getFolder();
    if (folder.isEmpty()) {
        moveToFolder(newEntity);
    } else {
        bool isDraft = findFolder(ApplicationDomain::SpecialPurpose::Mail::drafts) == folder;
        bool isSent = findFolder(ApplicationDomain::SpecialPurpose::Mail::sent) == folder;
        bool isTrash = findFolder(ApplicationDomain::SpecialPurpose::Mail::trash) == folder;
        mail.setDraft(isDraft);
        mail.setTrash(isTrash);
        mail.setSent(isSent);
    }

}

void SpecialPurposeProcessor::modifiedEntity(const Sink::ApplicationDomain::ApplicationDomainType &oldEntity, Sink::ApplicationDomain::ApplicationDomainType &newEntity)
{
    using namespace Sink::ApplicationDomain;
    auto mail = newEntity.cast<Mail>();
    //If we moved the mail to a non-specialpurpose folder explicitly, also clear the flags.
    if (mail.changedProperties().contains(Mail::Folder::name)) {
        auto folder = mail.getFolder();
        bool isDraft = findFolder(ApplicationDomain::SpecialPurpose::Mail::drafts) == folder;
        bool isSent = findFolder(ApplicationDomain::SpecialPurpose::Mail::sent) == folder;
        bool isTrash = findFolder(ApplicationDomain::SpecialPurpose::Mail::trash) == folder;
        mail.setDraft(isDraft);
        mail.setTrash(isTrash);
        mail.setSent(isSent);
    } else {
        moveToFolder(newEntity);
    }
}
