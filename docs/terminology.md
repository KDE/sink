Consistent, agreed-upon terminology is key to being able to work together efficiently on a complex software project such as this, particularly as we are building on the earlier Akonadi efforts which itself has established terminology. You can find the current glossary here.

It is recommended to familiarize yourself with the terms before going further into the design of Akonadi-next as it will make things clearer for you and easier to ask the questions you have in a way that others will understand immediately.

## Glossary of Akonadi-Next Terms
* akonadi1: The current akonadi implementation that uses a central server and an SQL database
* akonadi-next: This is the codename for the project. In the long run this is supposed to be folded into regular akonadi, so we will never release a product called akonadi-next.
* client: any application which accesses data using akonadi
* entity: The atomic unit for a given type of data. An email is an entity; an email folder is an entity; a calendar event is an entity; a contact is an entity; etc. Different kinds of entities may have their own data structure, but conceptually they are equivalent in most other ways.
* revision: A version of the store. One entity may have multiple revisions in a store, representing (for instance) the local state and the synchronized state of the entity.
* source: The canonical data set, which may be a remote IMAP server, a local iCal file, a local maildir, etc.
* store: The local, persistent (e.g. on disk) record of entities belonging to a source. This may be a full mirror of the data or simply metadata, a detail left up to the resource. The format of the data in the store is defined by the resource that owns it.
* resource: A plugin which provides client command processing, a store facade and synchronization for a given type of store. The resource also manages the configuration for a given source including server settings, local paths, etc.
* store facade: An object provided by resources which provides transformations between domain objects and the store.
* synchronizer: The operating system process responsible for overseeing the process of modifying and synchronizing a store. To accomplish this, a synchronizer loads the correct resource plugin, manages pipelines and handles client communication. One synchronizer is created for each source that is accessed by clients; these processes are shared by all clients.
* Preprocessor: A component that takes an entity and performs some modification of it (e.g. changes the folder an email is in) or processes it in some way (e.g. indexes it)
* pipeline: A run-time definable set of filters which are applied to an entity after a resource has performed a specific kind of function on it (create, modify, delete)
* query: A declarative method for requesting entities from one or more sources that match a given set of constraints
* command: Clients request modifications, additions and deletions to the store by sending commands to a synchronizer for processing
* command queue: A queue of commands kept by the synchronizer to ensure durability and, when necessary, replayability
* notification: A message sent from a synchronizer to inform the client of a change in the store
* domain object: An application domain object, i.e. an event.
* domain type: The type of a domain object. i.e. Akonadi2::ApplicationDomain::Event
* buffer: The buffers used by the resources to persist data in the datastore.
* buffer type: The individual buffer types as specified by the resource. These are internal types that don't necessarily have a 1:1 mapping to the domain types, although that is the default case that the default implementations expect.
