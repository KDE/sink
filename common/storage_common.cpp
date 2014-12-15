#include "storage.h"

#include <iostream>

namespace Akonadi2
{

void errorHandler(const Storage::Error &error)
{
    //TODO: allow this to be turned on / off globally
    //TODO: log $SOMEWHERE $SOMEHOW rather than just spit to stderr
    std::cerr << "Read error in " << error.store << ", code " << error.code << ", message: " << error.message << std::endl;
}

std::function<void(const Storage::Error &error)> Storage::basicErrorHandler()
{
    return errorHandler;
}

void Storage::read(const std::string &sKey, const std::function<bool(const std::string &value)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

void Storage::read(const std::string &sKey, const std::function<bool(void *ptr, int size)> &resultHandler)
{
    read(sKey, resultHandler, &errorHandler);
}

void Storage::scan(const std::string &sKey, const std::function<bool(void *keyPtr, int keySize, void *valuePtr, int valueSize)> &resultHandler)
{
    scan(sKey.data(), sKey.size(), resultHandler, &errorHandler);
}

} // namespace Akonadi2
