# Goals
## Axioms
1. Personal information is stored in multiple sources (address books, email stores, calendar files, ...)
2. These sources may local, remote or a mix of local and remote

## Requirements
1. Local mirrors of these sources must be available to 1..N local clients simultaneously
2. Local clients must be able to make (or at least request) changes to the data in the local mirrors
3. Local mirrors must be usable without network, even if the source is remote
4. Local mirrors must be able to syncronoize local changes to their sources (local or remote)
5. Local mirrors must be able to syncronize remote changes and propagate those to local clients
6. Content must be searchable by a number of terms (dates, identities, body text ...)
7. This must all run with acceptable performance on a moderate consumer-grade desktop system

Nice to haves:

1. As-close-to-zero-copy-as-possible for data
2. Simple change notification semantics
3. Resource-specific syncronization techniques
4. Data agnostic storage

Immediate goals:

1. Ease development of new features in existing resources
2. Ease maintenance of existing resources
3. Make adding new resources easy
4. Make adding new types of data or data relations easy
5. Improve performance relative to existing Akonadi implementation

Long-term goals:

1. Project view: given a query, show all items in all stores that match that query easily and quickly

Implications of the above:

* Local mirrors must support multi-reader, but are probably best served with single-writer semantics as this simplifies both local change recording as well as remote synchronization by keeping it in one process which can process write requests (local or remote) in sequential fashion.
* There is no requirement for a central server if the readers can concurrently access the local mirror directly
* A storage system which requires a schema (e.g. relational databases) are a poor fit given the desire for data agnosticism and low memory copying

# Overview

# Types
## Domain Type
The domain types exposed in the public interface.

## Buffer Type
The individual buffer types as specified by the resource. The are internal types that don't necessarily have a 1:1 mapping to the domain types, although that is the default case that the default implementations expect.

## Steps to add support for new types
* Add new type to applicationdomaintypes.h and implement `getTypenName()`
* Implement `TypeImplementation<>` for updating indexes etc.
* Add a type.fbs default schema for the type

## Steps for adding support for a type to a resource
* Add a TypeAdaptorFactory, which can either register resource specific mappers, or stick to what the default implementation in TypeImplementation provides
* Add a TypeFacade that injects the TypeAdaptorFactory in the GenericFacade
* Register the facade in the resource
* Add synchronization code that creates the relevant objects

# Change Replay
The change replay is based on the revisions in the store. Clients (and also the write-back mechanism), are informed that a new revision is available. Each client can then go through all new revisions (starting from the last seen revision), and thus update it's state to the latest revision.

# Tradeoffs/Design Decisions
* Key-Value store instead of relational
    * `+` Schemaless, easier to evolve
    * `-` No need to fully normalize the data in order to make it queriable. And without full normalization SQL is not really useful and bad performance wise. 
    * `-` We need to maintain our own indexes

* Individual store per resource
    * Storage format defined by resource individually
        * `-` Each resource needs to define it's own schema
        * `+` Resources can adjust storage format to map well on what it has to synchronize
        * `+` Synchronization state can directly be embedded into messages
    * `+` Individual resources could switch to another store technology
    * `+` Easier maintenance
    * `+` Resource is only responsible for it's own store and doesn't accidentaly break another resources store
    * `-` Inter`-`resource moves are both more complicated and more expensive from a client perspective
    * `+` Inter`-`resource moves become simple additions and removals from a resource perspective
    * `-` No system`-`wide unique id per message (only resource/id tuple identifies a message uniquely) 
    * `+` Stores can work fully concurrently (also for writing)

* Indexes defined and maintained by resources
    * `-` Relational queries accross resources are expensive (depending on the query perhaps not even feasible)
    * `-` Each resource needs to define it's own set of indexes
    * `+` Flexible design as it allows to change indexes on a per resource level
    * `+` Indexes can be optimized towards resources main usecases
    * `+` Indexes can be shared with the source (IMAP serverside threading)

* Shared domain types as common interface for client applications
    * `-` yet another abstraction layer that requires translation to other layers and maintenance
    * `+` decoupling of domain logic from data access
    * `+` allows to evolve types according to needs (not coupled to specific application domain types)

# Risks
* key-value store does not perform with large amounts of data
* query performance is not sufficient
* turnaround time for modifications is too high to feel responsive
* design turns out similarly complex as akonadi1
