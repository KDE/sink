The client API consists of:

* a modification API for entities (Create/Modify/Delete)
* a query API to retrieve entities
* a resource facade to abstract the resource implementation details
* a set of standardized domain types
* a notification mechanism to be notified about changes from individual stores

## Requirements/Design goals
* zero-copy should be possible (mmap support)
    * A single copy is more realistic
    * Most importantly we should hide how the data is stored (in parts, or one mmapped buffer)
* property-level on-demand loading of data
* streaming support for large properties (attachments)

## Store Facade
The store is always accessed through a store specific facade, which hides:

* store access (one store could use a database, and another one plain files)
* message type (flatbuffers, ...)
* indexes
* synchronizer communication
* notifications

This abstraction layer allows each resource to separately define how data is stored and retrieved. Therefore tradeoffs can be defined to suit the expected access patterns or structure of source data. Further it allows individual resources to choose different technologies as suitable. Logic can still be shared among resources while keeping the maintenance effort reasonable, by providing default implementations that are suitable for most workloads.

Because the facade also implements querying of indexes, a resource my use server-side searching to fullfill the query, and fall back to local searches when the server is not available.

## Modifications
Modifications are executed by the client sending modification commands to the synchronizer. The synchronizer is responsible for ensuring that modification are not lost and eventually persisted. A small window exists therefore, while a modification is transferred to the synchronizer, where a modification can get lost.

The API consists of the following calls:

* create(domainObject)
* modify(domainObject)
* remove(domainObject)

The changeset are recorded by the domain object when properties are set, and are then sent to the synchronizer once modify is called.

Each modification is associated with a specific revision, which allows the synchronizer to do automatic conflict resolution.

### Conflict Resolution
Conflicts can occur at two points:

* In the client: While i.e. an editor is open and we receive an update for the same entity
* In the synchronizer: After a modification is sent to the synchronizer but before it's processed

In the first case the client is repsonsible to resolve the conflict, in the latter case it's the synchronizer's responsibility.
A small window exists where the client has already started the modification (i.e. command is in socket), and a notification has not yet arrived that the same entity has been changed. In such a case the synchronizer may reject the modification because it has the revision the modification refers to no longer available.

### Lazy Loading ###
The system provides property-level lazy loading. This allows i.e. to defer downloading of attachments until the attachments is accessed, at the expense of having to have access to the source (which could be connected via internet).

To achieve this, the query system must check for the availability of all requested properties on all matched entities. If a property is not available, a command must be sent to the synchronizer to retrieve said properties. Once all properties are available the query can complete.

Note: We should perhaps define a minimum set of properties that *must* be available. Otherwise local search will not work. On the other hand, if a resource implements server-side search, it may not care if local search doesn't work.

### Data streaming ###
Large properties such as attachments should be streamable. An API that allows to retrieve a single property of a defined entity in a streamable fashion is probably enough.
