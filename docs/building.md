# Development Environment
We build and test Sink in a set of docker containers that can be build and used using a python based wrapper. This ensures reproducability and decouples the development environment from the host system (so upgrading your host system doesn't break all your builds). To avoid having to develop inside the container directly, source, build and install directories reside on the host system. 

On the host system the ~/kdebuild directory will be used (adujustable in settings.py), as root for all builds. Inside ~/kdebuild we have:

* $ENV/source/\*
* $ENV/build/\*
* $ENV/install/\*

$ENV is for this project always "sink".

## Setting it up
First clone the repository containing the docker files and the corresponding wrapper scripts [https://github.com/cmollekopf/docker.git]

It is recommended to setup an alias for the main script "testenv.py".
To do so add to your .bashrc, .zshrc or similar:
```
alias devenv='$PathToRepository/testenv.py' 
```

This guide will from here on use the "devenv" alias.

### Building the container
First you will need to build the docker container, so make sure you have docker setup and docker running (try "docker images"), otherwise follow your distributions instructions.

To build the relevant container use the following command:
```
devenv build kdesrcbuild fedora-kde
```

Various other distribution options exist but this is what we'll use by default.

### Initial build
Inside the container we use "kdesrcbuild" to build all necessary dependencies that the container doesn't already provide.

To execute an initial build use:
```
devenv srcbuild sink kdesrcbuild
```

This will start a temporary container, execute kdesrcbuild inside (using  docker.git/kdesrcbuild/sink/kdesrc-buildrc), and stop the container immediately.
Note that sed is run on the output that you see so all paths apply to the host system (so you can easily check the logfiles).

Once this succeeds you have Sink installed. Note however that it will only run inthe docker container and not on your host system.

### Daily business
You can work on the code as usual in "~/kdebuild/sink/source/sink" (dependencies are in the source directory as well).

To execute a regular build and install:
```
devenv srcbuild sink sink make install
```

To install kasync again:
```
devenv srcbuild sink kasync make install
```

To run kdesrcbuild on sink:
```
devenv srcbuild sink kdesrcbuild sink
```

To run sink tests :
```
devenv srcbuild sink sink make test
```

To get a shell in the docker container (You will find the kdebuild directory mounted in "/work/", kdesrcbuild is in "/home/developer/kdesrcbuild/":
```
devenv srcbuild sink shell
```

### Troubleshooting

* If your build starts failing misteriously, rerun kdesrcbuild to ensure all dependencies are up-to date.

* If kdesrcbuild fails, ensure ~/kdebuild is properly mounted into /work and that you have proper rights from both the container and the host (permissions can be a problem if docker creates the directories). To check from the container use "devenv srcbuild sink shell"

# Custom Build
While the above development environment is recommended for anyone interested in contribution to the project regularly, and the only way we can guarantee reproducability of test failures, it is of course also possible to build Sink regularly as any other cmake project. 

```
mkdir build && cd build
cmake ..
make install
```

# Dependencies

* ExtraCmakeModules >= 0.0.10
* Qt >= 5.2
* KF5::Async >= 0.1
* flatbuffers >= 1.0
* libgit2
* readline

## Maildir Resource
* KF5::Mime
