#include "storage.h"

#include <iostream>

void errorHandler(const Storage::Error &error)
{
    //TODO: allow this to be turned on / off globally
    //TODO: log $SOMEWHERE $SOMEHOW rather than just spit to stderr
    std::cerr << "Read error in " << error.store << ", code " << error.code << ", message: " << error.message << std::endl;
}

void Storage::read(const std::string &sKey, const std::function<bool(const std::string &value)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

void Storage::read(const std::string &sKey, const std::function<bool(void *ptr, int size)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

