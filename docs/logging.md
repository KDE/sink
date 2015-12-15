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

## Debug levels
* trace: trace individual codepaths. Likely outputs way to much information for all normal cases and likely is only ever temporarily enabled. Trace points are likely only inserted into code fragments that are known to be problematic.
* log: Comprehensive debug output. Enabled on demand
* warning: Only warnings, should always be logged.
* error: Critical messages that should never appear. Should always be logged.

## Collected information
Additionally to the regular message we want:
* pid
* threadid?
* timestamp
* sourcefile + position + function name
* application name
