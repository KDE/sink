## Steps to add support for new types
* Add new type to applicationdomaintypes.h and implement `getTypenName()`
* Implement `TypeImplementation<>` for updating indexes etc.
* Add a type.fbs default schema for the type

## Steps for adding support for a type to a resource
* Add a TypeAdaptorFactory, which can either register resource specific mappers, or stick to what the default implementation in TypeImplementation provides
* Add a TypeFacade that injects the TypeAdaptorFactory in the GenericFacade
* Register the facade in the resource
* Add synchronization code that creates the relevant objects

