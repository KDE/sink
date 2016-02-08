# Design Goals
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

