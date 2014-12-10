#pragma once

#include "client/clientapi.h"
#include "common/storage.h"

class ResourceAccess;

class DummyResourceFacade : public Akonadi2::StoreFacade<Akonadi2::Domain::Event>
{
public:
    DummyResourceFacade();
    virtual ~DummyResourceFacade();
    virtual void create(const Akonadi2::Domain::Event &domainObject);
    virtual void modify(const Akonadi2::Domain::Event &domainObject);
    virtual void remove(const Akonadi2::Domain::Event &domainObject);
    virtual void load(const Akonadi2::Query &query, const std::function<void(const Akonadi2::Domain::Event::Ptr &)> &resultCallback);

private:
    QSharedPointer<ResourceAccess> mResourceAccess;
};
