The client API consists of:

* a modification API for messages (Create/Modify/Delete)
* a query API to retrieve messages
* a resource facade to abstract the resource implementation details
* a set of standardized domain types
* a notification mechanism to be notified about changes from individual stores

## Requirements/Design goals
* zero-copy should be possible (mmap support)
    * Likely only possible until application domain until we rewrite portions of the applications
    * Most importantly we should hide how the data is stored (in parts, or one mmapped buffer)
    * Support for mmapped buffers implies that we keep track of the lifetime of the loaded values.
* property-level on-demand loading
* streaming support for certain properties (attachments)

## Domain Types
A set of standardized domain types is defined. This is necessary to decouple applications from resources (so a calendar can access events from all resources), and to have a "language" for queries.

The definition of the domain model directly affects:
* granularity for data retrievel (email property, or individual subject, date, ...)
* queriable properties (sender, id, ...)
* properties used for sorting (10 latest email)

The purpose of these domain types is strictly to be the interface and the types are not meant to be used by applications directly, or to be restricted by any other specifications (such as ical). By nature these types will be part of the evolving interface, and will need to be adjusted for every new property that an application must understand.

### Akonadi Domain Types
This is a proposed set of types that we will need to evolve into what we actually require. Hierarchical types are required to be able to query for a result set of mixed types.

Items:

* Item
    * incidence
        * Event
        * Todo
        * Journal
        * Freebusy
    * Note
    * Mail
    * Contact

Collections:

* Collection
    * Mail Folder
    * Calendar
    * Tasklist
    * Journal
    * Contact Group
    * Address Book

Relations:

* Relation
    * Tag

## Store Facade
The store is always accessed through a store specific facade, which hides:
* store access (one store could use a database, and another one plain files)
* message type (flatbuffers, ...)
* indexes
* syncronizer communication
* notifications

This abstraction layer allows each resource to separately define how data is stored and retrieved. Therefore tradeoffs can be defined to suit the expected access patters or structure of source data. Further it allows individual resources to choose different technologies as suitable. Logic can still be shared among resources, while keeping the maintenance effort reasonable, by providing default implementations that are suitable for most workloads.

Because the facade also implements querying of indexes, a resource my use server-side searching to fullfill the query, and fallback to local searches when the server is not available.

## Modifications
Modifications are stored by the client sending modification commands to the syncronizer. The syncronizer is responsible for ensuring that modification are not lost and eventually persistet. A small window exists therefore where a modification is transferred to the syncronizer where a modifications can get lost.

The API consists of the following calls:

* create(domainObject, resource)
* modify(domainObject, resource)
* remove(domainObject, resource)

The changeset can be recorded by the domain object adapter while the properties are set, and are then sent to the syncronizer once modify is called.

Each modification is associated with a specific revision, which allows the syncronizer to do automatic conflict resolution.

### Conflict Resolution
Conflicts can occur at two points in the client:

* While i.e. an editor is open and we receive an update for the same entity
* After a modification is sent to the syncronizer but before it's processed

In the first case the client is repsonsible to resolve the conflict, in the latter case it's the syncronizer's responsibility.
A small window exists where the client has already started the modification (i.e. command is in socket), and a notification has not yet arrived that the same entity has been changed. In such a case the syncronizer may reject the modification because it has the revision the modification refers to no longer available.

This design allows the syncronizer to be in control of the revisions, and keeps it from having to wait for all clients to update until it can drop revisions.

## Query System
The query system should allow for efficient retrieval for just the amount of data required by the client. Efficient querying will be supported by the indexes povided by the resources.

The query always retrieves a set of entities matching the query, while not necessarily all properties of the entity need to be populated.

Queries should be declarative to keep the specification simple and to allow the implementation to choose the most efficient execution.

Queries can be kept open to receive updates as the store changes, and modified to adjust the result set.

### Query
The query consists of:
* a declarative set of filters to match the wanted entities
* the set of properties to retrieve for each entity
* a limit for the amount of entities to retrieve
* an offset to retrieve more entities

Queryable properties are defined by the [[Domain Types]] above.

Other Requirements:
* modifiable: to facilitate adjustments, such as a date-range while scrolling in the mail view.
* serializable: to persist queries, i.e. to store a "smart folder" query to a config file.

#### Filter
A filter consists of:

* a property to filter on as defined by the [[Domain Types]]
* a comparator to use
* a value

The available comparators are:

* equal
* greater than
* less than
* inclusive range

Value types include:

* Null
* Bool
* Regular Expression
* Substring
* A type-specific literal value (e.g. string, number, date, ..)

Filters can be combined using AND, OR, NOT.

#### Example
```
query =  {
    offset: int
    limit: int
    filter = {
        and {
            collection = foo
            or {
                resource = res1
                resource = res2
            }
        }
    }
}
```

possible API:

```
query.filter().and().property("collection") = "foo"
query.filter().and().or().property("resource") = "res1"
query.filter().and().or().property("resource") = "res2"
query.filter().and().property("start-date") = InclusiveRange(QDateTime, QDateTime)
```

The problem is that it is difficult to adjust an individual resource property like that.

### Usecases ###
Mail:

* All mails in folder X within date-range Y that are unread.
* All mails (in all folders) that contain the string X in property Y.

Todos:

* Give me all the todos in that collection where their RELATED-TO field maps to no other todo UID field in the collection
* Give me all the todos in that collection where their RELATED-TO field has a given value
* Give me all the collections which have a given collection as parent and which have a descendant matching a criteria on its attributes;

Events:

* All events of calendar X within date-range Y.

Generic:
* entity with identifier X
* all entities of resource X

### Lazy Loading ###
The system provides property-level lazy loading. This allows i.e. to defer downloading of attachments until the attachments is accessed, at the expense of having to have access to the source (which could be connected via internet).

To achieve this, the query system must check for the availability of all requested properties on all matched entities. If a property is not available the a command should be sent to the synchronizer to retrieve said properties. Once all properties are available the query can complete.

Note: We should perhaps define a minimum set of properties that *must* be available. Otherwise local search will not work. On the other hand, if a resource implements server-side search, it may not care if local search doesn't work.

### Data streaming ###
Large objects such as attachments should be streamable. An API that allows to retrieve a single property of a defined entity in a streamable fashion is probably enough.

### Indexes ###
Since only properties of the domain types can be queried, default implementations for commonly used indexes can be provided. These indexes are populated by generic preprocessors that use the domain-type interface to extract properties from individual entites.

## Notifications ##
A notification mechanism is required to inform clients about changes. Running queries will automatically update the result-set if a notification is received.

A notification constist of:

* The latest revision of the store
* A hint what properties changed

The revision allows the client to only fetch the data that changed.
The hint allows the client to avoid fetching that it's not interested in.
A running query can do all of that transparently behind the scenes.

Note that the hints should indeed only hint what has changed, and not supply the actual changeset. These hints should be tailored to what we see as useful, and must therefore be easy to modify.

