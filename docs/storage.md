## Storage Model
The storage model is simple:
```
Entity {
  Id
  Revision {
      Revision-Id,
      Property*
  }+
}*
```

The store consists of entities that have each an id and a set of properties. Each entity can have multiple revisions.

A entity is uniquely identified by:

* Resource + Id

The additional revision identifies a specific instance/version of the entity.

Uri Scheme:
```
sink://resource/id:revision
```

## Store Entities
Each entity can be as normalized/denormalized as useful. It is not necessary to have a solution that fits everything.

Denormalized:

* priority is that the mime message stays intact (signatures/encryption)

```
Mail {
  id
  mimeMessage
}
```

Normalized:

* priority is that we can access individual members efficiently.
* we don't care about exact reproducability of e.g. an ical file
```
Event {
  id
  subject
  startDate
  attendees
  ...
}
```

Of course any combination of the two can be used, including duplicating data into individual properties while keeping the complete struct intact.

#### Optional Properties
For each domain type, we want to define a set of required and a set of optional properties. The required properties are the minimum bar for each resource, and are required in order for applications to work as expected. Optional properties may only be shown by the UI if actually supported by the backend.

However, we'd like to be able to support local-only storage for resources that don't support an optional property. Each entity thus has a "local" buffer that provides default local only storage. This local-only buffer provides storage for all properties of the respective domain type.

Each resource can freely define how the properties are split, while it wants to push as many as possible into the left side so they can be synchronized. Note that the resource is free to add more properties to it's synchronized buffer even though they may not be required by the specification.

The advantage of this is that a resource only needs to specify a minimal set of properties, while everything else is taken care of by the local-only buffer. This is supposed to make it easier for resource implementors to get something working.

### Value Format
Each entity-value in the key-value store consists of the following individual buffers:

* Metadata: metadata that is required for every entity (revision, ....)
* Local: default storage buffer that is domain-type specific.
* Resource: the buffer defined by the resource (additional properties, values that help for synchronization)

## Database
### Database Layout
Storage is split up in multiple named databases that reside in the same database environment.

```
 $DATADIR/storage/$RESOURCE_IDENTIFIER/
```

* $BUFFERTYPE.main: The primary store for a type
* $BUFFERTYPE.index.$PROPERTY: Secondary indexes
* revisionType: Allows to lookup the type by revision to find the correct primary or secondary db's.
* revisions: Allows to lookup the entity id by revision

The resource can be effectively removed from disk (besides configuration),
by deleting the directories matching `$RESOURCE_IDENTIFIER*` and everything they contain.

#### Design Considerations
The stores are split by buffertype, so a full scan (which is done by type), doesn't require filtering by type first. The downside is that an additional lookup is required to get from revision to the data.

### Revisions
Every operation (create/delete/modify), leads to a new revision. The revision is an ever increasing number for the complete store.

Each entity is stored with a key consisting of its id and the revision. This way it is possible to lookup older revision.

Removing an entity simply results in a new revision of the entitiy recording the removal.

Secondary indexes always refer to the latest revision.

#### Design Considerations
By having one revision for the complete store instead of one per type, the change replay always works across all types. This is especially important in the write-back mechanism that replays the changes to the source.

### Revision cleanup
Because the store would grow infinitely, old revisions need to be removed.
The resource maintains a "lower bound revision", which is the lowest revision of any change-replaying component (such as clients and write-back).
For all lower revisions the cleanup will remove any revision that:

* is a delete command (the revision is no longer existing)
* has a newer revision for the same entity (the revision is outdated)

By doing cleanups continously, we avoid keeping outdated data.

### BLOB properties
Files are used to handle opaque large properties that should not end up in memory. BLOB properties are in their nature never queriable (extract parts of it to other properties if indexes are required).

For reading:

Resources...

* store the file in $DATADIR/storage/$RESOURCE_IDENTIFIER.files/
* store the filename in the blob property.
* delete the file when the corresponding entity is deleted.

Queries...

* Copy the requested property to /tmp/sink/client_files/ and provide the path in the property
* The file is guaranteed to exist for the lifetime of the query result.

Clients..

* Load the file from disk and use it as they wish (moving is fine too)

For writing:

Clients..

* Request a path from sink and store the file there.
* Store the path of the written file in the property.

Resources..

* move the file to $DATADIR/storage/$RESOURCE_IDENTIFIER.files/
* store the new path in the entity

#### Design Considerations
Using regular files as the interface has the advantages:

* Existing mechanisms can be used to stream data directly to disk.
* The necessary file operations can be efficiently handled depending on OS and filesystem.
* We avoid reinventing the wheel.

The copy is necessary to guarantee that the file remains for the client/resource even if the resource removes the file on it's side as part of a sync.
The copy could be optimized by using hardlinks, which is not a portable solution though. For some next-gen copy-on-write filesystems copying is a very cheap operation.

A downside of having a file based design is that it's not possible to directly stream from a remote resource i.e. into the application memory, it always has to go via a file.

## Database choice
By design we're interested in key-value stores or perhaps document databases. This is because a fixed schema is not useful for this design, which makes
SQL not very useful (it would just be a very slow key-value store). While document databases would allow for indexes on certain properties (which is something we need), we did not yet find any contenders that looked like they would be useful for this system.

### Requirements
* portable; minimally: Linux, Windows, MacOS X
* multi-thread and multi-process concurrency with single writer and multiple readers.
    * This is required so we don't block clients while a resource is writing and deemed essential for performance and to reduce complexity.
* Reasonably fast so we can implement all necessary queries with sufficient performance
* Can deal with large amounts of data
* On disk storage with ACID properties.
* Memory consumption is suitable for desktop-system (no in-memory stores).

Other useful properties:

* Is suitable to implement some indexes (the fewer tools we need the better)
* Support for transactions
* Small overhead in on-disk size

### Contenders
* LMDB
    * support for mmapped values
    * good read performance, ok write performance
    * fairly complex API
    * Up to double storage size due to paging (with 4k pagesize 4001 bytes provide the worst case)
    * size limit of 4GB on 32bit systems, virtually no limit on 64bit.  (leads to 2GB of actual payload with write amplification)
    * limited key-search capabilities
    * ACID transactions
    * MVCC concurrency
    * no compaction, database always grows (pages get reused but are never freed)
* berkeley db (bdb)
    * performance is supposedly worse than lmdb (lmdb was written as successor to bdb for openldap)
    * oracle sits behind it (it has an AGPL licence though)
* rocksdb
    * => no multiprocess
* kyotocabinet http://fallabs.com/kyotocabinet/
    * fast, low on-disk overhead, simple API
    * => no multiprocess
    * GPL
* hamsterdb
    * => no multiprocess
* sqlite4
    * not yet released
* bangdb
    * not yet released opensource, looks promising on paper
* redis
    * => loads everything into memory
    * => not embeddable
* couchdb
    * MVCC concurrency
    * document store
    * not embeddable (unless we write sink in erlang ;)
* https://github.com/simonhf/sharedhashfile
    * not portable (i.e. Windows; it's a mostly-Linux thing)
* http://sphia.org/architecture.html
    * => no multiprocess
* leveldb
    * => no multiprocess
* ejdb http://ejdb.org/#ejdb-c-library
    * modified version of kyoto cabinet
    * => multiprocess requires locking, no multiprocess
    * Is more of a document store
    * No updates since September 2013
* http://unqlite.org
    * bad performance with large database (looks like O(n))
    * like lmdb roughly 2\*datasize
    * includes a document store
    * mmapped ready access
    * reading about 30% the speed of lmdb
    * slow writes with transactions

### Result
LMDB was chosen as one of the few contenders that are embeddable and have true multi process support. It also outperformed unqllite significantly, although its write performance and disk usage aren't ideal.

### Indexes
Additionally to the primary store, indexes are required for efficient lookups.

Since indexes always need to be updated they directly affect how fast we can write data. While reading only a subset of the available indexes is typically used, so a slow index doesn't affect all quries.

#### Contenders
* xapian:
    * fast fulltext searching
    * No MVCC concurrency
    * Only supports one writer at a time
    * If a reader is reading blocks that have now been changed by a writer, it throws a DatabaseModifiedException. This means most of the Xapian code needs to be in `while (1) { try { .. } catch () }` blocks and needs to be able to start from scratch.
    * Wildcard searching (as of 2015-01) isn't ideal. It works by expanding the word into all other words in the query and that typically makes the query size huge. This huge query is then sent to the database. Baloo has had to configure this expanding of terms so that it consumes less memory.
    * Non existent UTF support - It does not support text normalization and splitting the terms at custom characters such as '\_'.
* lmdb:
    * sorted keys
    * sorted duplicate keys
    * No FTS
    * MVCC concurrency
* sqlite:
    * SQL
    * updates lock the database for readers
    * concurrent reading is possible
    * Requires duplicating the data. Once in a column as data and then in the FTS.
* lucenePlusPlus
    * fast full text searching
    * MVCC concurrency

### Result
For regular secondary indexes LMDB is used as well, because it's sufficient for key lookups, and by using the same database, we can store the indexed data directly in the same transaction.

No solution for full-text indexes has been chosen yet. Baloo implements a fulltext index on top of LMDB though.

## Useful Resources
* LMDB
    * Wikipedia for a good overview: <https://en.wikipedia.org/wiki/Lightning_Memory-Mapped_Database>
    * Benchmarks: <http://symas.com/mdb/microbench/>
    * Tradeoffs: <http://symas.com/is-lmdb-a-leveldb-killer/>
    * Disk space benchmark: <http://symas.com/mdb/ondisk/>
    * LMDB instead of Kyoto Cabinet as redis backend: <http://www.anchor.com.au/blog/2013/05/second-strike-with-lightning/>

