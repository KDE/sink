The sink shell is the primary interaction point from the commandline. It can be used for debugging, maintenance and scripting.

The syntax is:
  `sinksh COMMAND TYPE ...`

# Commands

## list
The list command allows to execute queries and retreive results in form of lists.
Eventually you will be able to specify which properties should be retrieved, for now it's a hardcoded list for each type. It's generally useful to check what the database contains and whether queries work.

## count
Like list, but only output the result count.

## stat
Some statistics how large the database is, how the size is distributed accross indexes, etc.

## create/modify/delete
Allows to create/modify/delete entities. Currently this is only of limited use, but works already nicely with resources. Eventually it will allow to i.e. create/modify/delete all kinds of entities such as events/mails/folders/....

## clear
Drops all caches of a resource but leaves the config intact. This is useful while developing because it i.e. allows to retry a sync, without having to configure the resource again.

## synchronize
Allows to synchronize a resource. For an imap resource that would mean that the remote server is contacted and the local dataset is brought up to date,
for a maildir resource it simply means all data is indexed and becomes queriable by sink.

Eventually this will allow to specify a query as well to i.e. only synchronize a specific folder.

## show
Provides the same contents as "list" but in a graphical tree view. This was really just a way for me to test whether I can actually get data into a view, so I'm not sure if it will survive as a command. For the time being it's nice to compare it's performance to the QML counterpart.

# Setting up a new resource instance
sink_cmd is already the primary way how you create resource instances:

  `sinksh create resource org.kde.maildir path /home/developer/maildir1`

This creates a resource of type "org.kde.maildir" and a configuration of "path" with the value "home/developer/maildir1". Resources are stored in configuration files, so all this does is write to some config files.

  `sinksh list resource`

By listing all available resources we can find the identifier of the resource that was automatically assigned.

  `sinksh synchronize org.kde.maildir.instance1`

This triggers the actual synchronization in the resource, and from there on the data is available.

  `sinksh list folder org.kde.maildir.instance1`

This will get you all folders that are in the resource.

  `sinksh remove resource org.kde.maildir.instance1`

And this will finally remove all traces of the resource instance.
