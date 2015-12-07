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

## Domain Types
A set of standardized domain types is defined. This is necessary to decouple applications from resources (so a calendar can access events from all resources), and to have a "language" for queries.

The definition of the domain model directly affects:
* granularity for data retrieval (email property, or individual subject, date, ...)
* queriable properties for filtering and sorting (sender, id, ...)

The purpose of these domain types is strictly to be the interface and the types are not necessarily meant to be used by applications directly, or to be restricted by any other specifications (such as ical). By nature these types will be part of the evolving interface, and will need to be adjusted for every new property that an application must understand.

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

* While i.e. an editor is open and we receive an update for the same entity
* After a modification is sent to the synchronizer but before it's processed

In the first case the client is repsonsible to resolve the conflict, in the latter case it's the synchronizer's responsibility.
A small window exists where the client has already started the modification (i.e. command is in socket), and a notification has not yet arrived that the same entity has been changed. In such a case the synchronizer may reject the modification because it has the revision the modification refers to no longer available.

This design allows the synchronizer to be in control of the revisions, and keeps it from having to wait for all clients to update until it can drop revisions.

## Query System
The query system should allow for efficient retrieval for just the amount of data required by the client. Efficient querying is supported by the indexes provided by the resources.

The query always retrieves a set of entities matching the query, while not necessarily all properties of the entity need to be populated.

Queries should are declarative to keep the specification simple and to allow the implementation to choose the most efficient execution.

Queries can be kept open (live) to receive updates as the store changes.

### Query
The query consists of:
* a set of filters to match the wanted entities
* the set of properties to retrieve for each entity

Queryable properties are defined by the [[Domain Types]] above.

### Query Result
The result is returned directly after running the query in form of a QAbstractItemModel. Each row in the model represents a matching entity.

The model allows to access the domain object directly, or to access individual properties directly via the rows columns.

The model is always populated asynchronously. It is therefore initially empty and will then populate itself gradually, through the regular update mechanisms (rowsInserted).

Tree Queries allow the application to query for i.e. a folder hierarchy in a single query. This is necessary for performance reasons to avoid recursive querying in large hierarchies. To avoid on the other hand loading large hierchies directly into memory, the model only populates the toplevel rows automatically, all other rows need to be populated by calling `QAbstractItemModel::fetchMore(QModelIndex);`. This way the resource can deal efficiently with the query (i.e. by avoiding the many roundtrips that would be necessary with recursive queries), while keeping the amount of data in memory to a minimum (i.e. if the majority of the folder tree is collapsed in the view anyways). A tree result set can therefore be seen as a set of sets, where every subset corresponds to the children of one parent.

If the query is live, the model updates itself if the update applies to one of the already loaded subsets (otherwise it's currently irrelevant and will load once the subset is loaded).

#### Enhancements
* Asynchronous loading of entities/properties can be achieved by returning an invalid QVariant initially, and emitting dataChanged once the value is loaded.
* To avoid loading a large list when not all data is necessary, a batch size could be defined to guarantee for instance that there is sufficient data to fill the screen, and the fetchMore mechanism can be used to gradually load more data as required when scrolling in the application.

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

To achieve this, the query system must check for the availability of all requested properties on all matched entities. If a property is not available, a command must be sent to the synchronizer to retrieve said properties. Once all properties are available the query can complete.

Note: We should perhaps define a minimum set of properties that *must* be available. Otherwise local search will not work. On the other hand, if a resource implements server-side search, it may not care if local search doesn't work.

### Data streaming ###
Large properties such as attachments should be streamable. An API that allows to retrieve a single property of a defined entity in a streamable fashion is probably enough.

### Indexes ###
Since only properties of the domain types can be queried, default implementations for commonly used indexes can be provided. These indexes are populated by generic preprocessors that use the domain-type interface to extract properties from individual entites.

## Notifications ##
A notification mechanism is required to inform clients about changes. Running queries will automatically update the result-set if a notification is received.

Note: A notification could supply a hint on what changed, allowing clients to ignore revisions with irrelevant changes. 
A running query can do all of that transparently behind the scenes. Note that the hints should indeed only hint what has changed, and not supply the actual changeset. These hints should be tailored to what we see as useful, and must therefore be easy to modify.
