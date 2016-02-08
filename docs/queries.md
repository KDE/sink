## Query System
The query system should allow for efficient retrieval for just the amount of data required by the client. Efficient querying is supported by the indexes provided by the resources.

The query always retrieves a set of entities matching the query, while not necessarily all properties of the entity need to be populated.

Queries are declarative to keep the specification simple and to allow the implementation to choose the most efficient execution.

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

