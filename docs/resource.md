The resource consists of:

* the syncronizer process
* a plugin providing the client-api facade
* a configuration setting up the filters

# Synchronizer
* The synchronization can either:
    * Generate a full diff directly on top of the db. The diffing process can work against a single revision/snapshot (using transactions). It then generates a necessary changeset for the store.
    * If the source supports incremental changes the changeset can directly be generated from that information.

The changeset is then simply inserted in the regular modification queue and processed like all other modifications.
The synchronizer already knows that it doesn't have to replay this changeset to the source, since replay no longer goes via the store.

# Preprocessors
Preprocessors are small processors that are guaranteed to be processed before an new/modified/deleted entity reaches storage. The can therefore be used for various tasks that need to be executed on every entity.

Usecases:

* Updating various indexes
* detecting spam/scam mail and setting appropriate flags
* email filtering

Preprocessors need to be fast, since they directly affect how fast a message is processed by the system.

The following kinds of preprocessors exist:

* filtering preprocessors that can potentially move an entity to another resource
* passive filter, that extract data that is stored externally (i.e. indexers)
* flag extractors, that produce data stored with the entity (spam detection)

Filter typically be read-only, to i.e. not break signatures of emails. Extra flags that are accessible through the akonadi domain model, can therefore be stored in the local buffer of each resource.

# Generic Preprocessors
Most preprocessors will likely be used by several resources, and are either completely generic, or domain specific (such as only for mail).
It is therefore desirable to have default implementations for common preprocessors that are ready to be plugged in.

The domain types provide a generic interface to access most properties of the entities, on top of which generic preprocessors can be implemented.
It is that way trivial to i.e. implement a preprocessor that populates a hierarchy index of collections.

# Pipeline
A pipeline is an assembly of a set of preprocessors with a defined order. A modification is always persisted at the end of the pipeline once all preprocessors have been processed.
