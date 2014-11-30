#include <lmdb.h>
#include <string>
#include <QString>

class Database {
public:
    Database(const QString &path);
    ~Database();
    MDB_txn *startTransaction();
    void endTransaction(MDB_txn *transaction);
    void write(const std::string &sKey, const std::string &sValue, MDB_txn *transaction);
    void read(const std::string &sKey);

private:
    MDB_env *env;
    MDB_dbi dbi;
};
