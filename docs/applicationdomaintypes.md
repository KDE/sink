## Domain Types
A set of standardized domain types is defined. This is necessary to decouple applications from resources (so a calendar can access events from all resources), and to have a "language" for queries.

The definition of the domain model directly affects:

* granularity for data retrieval (email property, or individual subject, date, ...)
* queriable properties for filtering and sorting (sender, id, ...)

The purpose of these domain types is strictly to be the interface and the types are not meant to be used by applications directly, or to be restricted by any other specifications (such as ical). By nature these types will be part of the evolving interface, and will need to be adjusted for every new property that an application accesses.

### Application Domain Types
This is the currently defined set of types. Hierarchical types are required to be able to query for a result set of mixed types, but are not necessarily structured as such in the inheritance model.

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
            * Sink Resource
            * Mail Folder
            * Calendar
            * Todolist
            * Journal
            * Address Book
        * Relation
            * Tag
            * Contact Group
            * Thread
    * Sink Resource
        * Maildir Resource
        * IMAP Resource
    * Account

#### Properties
```no-highlight
Entity: The smallest unit in the sink universe
    id [QByteArray]: unique identifier in the sink storage. Not persistant over db recreations and can therefore only be referenced from within the sink database.
```
```no-highlight
Domain Object:
    uid [QByteArray}: unique identifier of the domain object.
    revision [int]: revision of the entity
    resource [SinkResource.id]: The parent resource.
```
```no-highlight
Event:
    summary [QString]: A string containing a short summary of the event.
    startDate [QDateTime]: The start date of the event.
    startTime [QDateTime]: The start time of the event. Optional.
```
```no-highlight
Mail:
    uid [QByteArray]: The message id.
    subject [QString]: The subject of the email.
    folder [MailFolder.id]: The parent folder.
    date [QDateTime]: The date of the email.
    mimeMessage [QString]: A string containing the path to the mime message
```
```no-highlight
Mail Folder:
    parent [MailFolder.id]: The parent folder.
    name [QString]: The user visible name of the folder.
    icon [QString]: The name of the icon of the folder.
```
```no-highlight
Sink Resource:
    type [QByteArray]: The type of the resource.
    name [QString]: The name of the resource.
    account [Account.id]: The identifier of the associated account.
```
```no-highlight
Maildir Resource:
    path [QString]: The path to the maildir.
```
```no-highlight
Account:
    name [QString]: The name of the account.
    icon [QString]: The name of the icon of the account.
```

### References/Hierachies
Some domain objects reference others, and that is often used to build hierarchies.
Examples are folder hierachies, tags, todo hierarchies, mail threads, contact groups, etc.

These references can be built on two levels:
* On the sink entity level: The referenced object *must* be available in local storage, and we're only linking to that specific instance. If the referenced entity is removed, the reference breaks. The reference always only references a single sink entity.
* On the domain object level: The reference can remain also if no object currently matches the reference. The reference automatically applies to new entities containing an object with the referenced uid. More than one entity can be matched if they contain the same domain object.

#### Examples
The following hierachies exist among others:

* Parent Collection
    * Given by the source (path of the folder in IMAP)
    * Parent folder "owns" the sub entity
    * Link exists on the sink entity level: We specify where the entity lives, this MUST always be a single parent entity.
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
