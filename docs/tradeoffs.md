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
* design turns out similarly complex as Akonadi
