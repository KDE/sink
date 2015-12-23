## Domain Types
A set of standardized domain types is defined. This is necessary to decouple applications from resources (so a calendar can access events from all resources), and to have a "language" for queries.

The definition of the domain model directly affects:
* granularity for data retrieval (email property, or individual subject, date, ...)
* queriable properties for filtering and sorting (sender, id, ...)

The purpose of these domain types is strictly to be the interface and the types are not meant to be used by applications directly, or to be restricted by any other specifications (such as ical). By nature these types will be part of the evolving interface, and will need to be adjusted for every new property that an application accesses.

### Application Domain Types
This is a proposed set of types that we will need to evolve into what we actually require. Hierarchical types are required to be able to query for a result set of mixed types.

* Entity
    * Domain Object
        * Incidence
            * Event
            * Todo
            * Journal
            * Freebusy
        * Note
        * Mail
        * Contact
        * Collection
            * Akonadi Resource
            * Mail Folder
            * Calendar
            * Todolist
            * Journal
            * Address Book
        * Relation
            * Tag
            * Contact Group
            * Thread
    * Akonadi Resource
        * Maildir Resource

#### Properties
```no-highlight
Entity: The smallest unit in the akonadi universe
    id: unique identifier in the akonadi storage. Not persistant over db recreations and can therefore only be referenced from within the akonadi database.
```
```no-highlight
Domain Object:
    uid: unique identifier of the domain object.
    revision: revision of the entity
    resource: reference to AkonadiResource:id of the parent resource.
```
```no-highlight
Event:
    summary: A string containing a short summary of the event.
    startDate: The start date of the event.
    startTime: The start time of the event. Optional.
```
```no-highlight
Mail:
    uid: The message id.
    subject: The subject of the email.
    folder: The identifier of the parent folder.
    date: The date of the email.
    mimeMessage: A string containing the path to the mime message
```
```no-highlight
Akonadi Resource:
    type: The type of the resource.
    name: The name of the resource.
```
```no-highlight
Maildir Resource:
    path: The path to the maildir.
```

### References/Hierachies
Some domain objects reference others, and that is often used to build hierarchies.
Examples are folder hierachies, tags, todo hierarchies, mail threads, contact groups, etc.

These references can be built on two levels:
* On the akonadi entity level: The referenced object *must* be available in local storage, and we're only linking to that specific instance. If the referenced entity is removed, the reference breaks. The reference always only references a single akonadi entity.
* On the domain object level: The reference can remain also if no object currently matches the reference. The reference automatically applies to new entities containing an object with the referenced uid. More than one entity can be matched if they contain the same domain object.

#### Examples
The following hierachies exist among others:

* Parent Collection
    * Given by the source (path of the folder in IMAP)
    * Parent folder "owns" the sub entity
    * Link exists on the akonadi entity level: We specify where the entity lives, this MUST always be a single parent entity.
* Subtodos
    * Given by the todo itself
    * Not necessarly owning (though often implemented as such, similar to threading)
    * Link exists on domain object level.
* Mail Threads
    * Non owning, but a mail always only belongs to one thread.
    * Hierarchy given by message-id references in headers and subject + date.
    * Link exists on domain object level.
* Contact Groups
    * A contact can belong to many groups and the reference is non-owning.
    * Link exists on domain object level.
* Tags
    * An entity can have many tags
    * The tag references the entity, not the other way around.
    * Link exists on domain object level.

#### Example queries:
* All mail folders of a resource
* All threads within date-time range
