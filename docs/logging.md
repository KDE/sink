For debugging purposes a logging framework is required. Simple qDebugs() proved to be insufficient for any non-trivial software. Developers should be enabled to add detailed debug information that allows to analyze problems, and users should be enabled to record that information at runtime to debug a problem. The aim is to get away from the situation where developers remove messages because "it's to noisy", and then have to ship a special version with additional debug output to a user to debug a problem, just to then remove the debug output again.

## Requirements
* runtime configurability of debug level for specific components
* queriable debug logs. If everything is turned on, a *lot* of information will be generated.
* integration with the system log. It likely makes sense to integrate with the system-log instead of rolling our own solution or use .xsession-errors as dumping ground. In any case, simply logging to the console is not enough.
* debug information *must* be available in release builds
* It may make sense to be able to disable certain debug output (configurable per debug level) for certain components at compile time, for performance reasons.
* Ideally little interaction with stdout (i.e. only warnings). Proper monitoring should happen through:
    * logfiles
    * a commandline monitor tool
    * some other developer tool
This way we get complete logs also if some resource was not started from the console (i.e. because it was already running).

## Debug levels
* trace: trace individual codepaths. Likely outputs way to much information for all normal cases and likely is only ever temporarily enabled for certain areas.
* log: Comprehensive debug output. Enabled on demand
* warning: Only warnings, should always be logged.
* error: Critical messages that should never appear. Should always be logged.

## Debug areas and components
Debug areas and components split the code into sections that can be enabled/disabled as one. This gives finer grained control over what is logged or displayed.

Debug areas are the static part, that typically represent a class or file, but give no information about which runtime-component is executing the given code.

Components are the runtime information part that can represent i.e. the resource plugin in the client process or the resource process itself.

The full debugging area is then assembled as: "Component.Area"

This can result in identifiers like:

* $RESOURCE_IDENTIFIER.sync.performance
* $RESOURCE_IDENTIFIER.sync
* $RESOURCE_IDENTIFIER.communication
* $RESOURCE_IDENTIFIER.pipeline
* kube.$RESOURCE_IDENTIFIER.communication
* kube.$RESOURCE_IDENTIFIER.queryrunner
* kube.actions

## Logging guidelines
* The trace log level should be used for any information that is not continuously required.
* Messages on the Log level should scale. During a sync with 10k messages we don't want 10k messages on the log level, these should go to trace.

## Collected information
Additionally to the regular message we want:

* pid
* threadid?
* timestamp
* sourcefile + position + function name
* application name / resource identifier
* area (i.e. resource access)
