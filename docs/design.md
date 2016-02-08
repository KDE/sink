# Overview

Sink is a data access layer that additionally handles synchronization with external sources and indexing of data for efficient queries.

## Client API
The client facing API hides all Sink internals from the applications and emulates a unified store that provides data through a standardized interface. 
This allows applications to transparently use various data sources with various data source formats.

## Resource
A resource is a plugin that provides access to an additional source. It consists of a store, a synchronizer process that executes synchronization & change replay to the source and maintains the store, as well as a facade plugin for the client api.

## Store
Each resource maintains a store that can either store the full dataset for offline access or only metadata for quick lookups. Resources can define how data is stored.

## Types
### Domain Type
The domain types exposed in the public interface.

### Buffer Type
The individual buffer types as specified by the resource. The are internal types that don't necessarily have a 1:1 mapping to the domain types, although that is the default case that the default implementations expect.

## Mechanisms
### Change Replay
The change replay is based on the revisions in the store. Clients (as well as also the write-back mechanism that replays changes to the source), are informed that a new revision is available. Each client can then go through all new revisions (starting from the last seen revision), and thus update its state to the latest revision.

### Preprocessor pipeline
Each resource has an internal pipeline of preprocessors that can be used for tasks such as indexing or filtering. The pipeline guarantees that the preprocessor steps are executed before the entity is persisted.

