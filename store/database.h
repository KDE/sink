#include <lmdb.h>
#include <string>

class Database {
public:
    Database();
    ~Database();
    MDB_txn *startTransaction();
    void endTransaction(MDB_txn *transaction);
    void write(const std::string &sKey, const std::string &sValue, MDB_txn *transaction);
    void read(const std::string &sKey);

private:
    MDB_env *env;
    MDB_dbi dbi;
};
