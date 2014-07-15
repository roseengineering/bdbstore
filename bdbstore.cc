
/***
Copyright 2014 George Magiros under the terms of the MIT LICENSE.

bdbstore
=============

This library package wraps the Berkeley DB version 5.3 C API.  

Besides supporting key-value database operations, this package also
supports cursor operations as well as transactions.  Configuration
oriented API calls were excluded since they can usually be better set
using Berkeley DB's DB\_CONFIG file.  In addition, this package
supports the ability to retrieve multiple key-value pairs with a
single call.

The Berkeley DB library comes preinstalled on most Unix like systems.
When installed using npm, the package will attempt to compile and link
itself to the Berkeley DB library.  Therefore, bdbstore requires
that g++ and the Berkeley DB C library development files are already
installed on your system.  On Ubuntu, these dependencies can be
resolved with the command:

    $ sudo apt-get install g++ libdb5.3-dev

To install the library package within your application run:

    $ npm install git://github.com/roseengineering/bdbstore.git

To run the unit tests on the library, execute the following commands 
inside the package's directory:

    $ npm install
    $ npm test
*/

#include <node.h>
#include <db.h>
#include <v8.h>

#include <cstring>   // strlen, memcpy, memset
#include <cstdlib>   // malloc and free

using namespace v8;

#define BUFFER_LENGTH   (5 * 1024 * 1024)   // 5MB

// general macros
#define SET_VALUE(obj, name, value)     obj->Set(String::NewSymbol(name), value)
#define GET_VALUE(obj, name)            obj->Get(String::NewSymbol(name))
#define RETURN_OBJECT(obj)              HandleScope scope; return scope.Close(obj)
#define SET_FUNCTION(obj, name, value)  SET_VALUE(obj, name, FunctionTemplate::New(value)->GetFunction())
#define SET_EXTERNAL(obj, name, value)  SET_VALUE(obj, name, External::Wrap(value))

#define GET_EXTERNAL(obj, name)         (obj->IsObject() ? External::Unwrap(GET_VALUE(obj, name)) : 0)
#define GET_BOOLEAN(obj, name)          (obj->IsObject() ? GET_VALUE(obj, name)->BooleanValue() : false)

// function like macros
#define GET_DB      DB *db = (DB*) GET_EXTERNAL(args.This()->ToObject(), "_db")
#define GET_DBCUR   DBC *cur = (DBC*) GET_EXTERNAL(args.This()->ToObject(), "_cur")
#define GET_DBTXN   DB_TXN *txn = (DB_TXN*) GET_EXTERNAL(args.This()->ToObject(), "_txn")

#define IF_TRUE_SET_FLAG(name, value)   if (GET_BOOLEAN(target, name)) flags |= value
#define SET_METHOD(name, value)         SET_FUNCTION(target, name, value)
#define RETURN_ERR                      RETURN_OBJECT(err_object(ret));
#define RETURN_UNDEFINED                RETURN_OBJECT(Undefined());

#define CHECK_NUMARGS(n, m) \
    if (args.Length() < n || args.Length() > m) { \
        ThrowException(Exception::TypeError(String::New("Wrong number of arguments"))); \
        RETURN_UNDEFINED; \
    }

#define CHECK_CALLBACK \
    if (!args[args.Length() - 1]->IsFunction()) { \
        ThrowException(Exception::TypeError(String::New("Callback is not a function"))); \
        RETURN_UNDEFINED; \
    }

// global variables

DB_ENV *dbenv = NULL;

// prototypes

Local<Object> db_object(DB*, DB_TXN*);
Local<Object> env_object();
Handle<Value> err_object(int);
Local<Object> txn_object(DB_TXN *);
Local<Object> cursor_object(DBC *);

// async functions

typedef struct AsyncData {
    DB *db;      // input parameters
    DB_TXN *txn;
    DB_TXN *parent;
    DBC *cur;
    DBT *key_dbt;
    DBT *data_dbt;
    u_int32_t flags;
    char *key, *value;
    Persistent<Function> callback;
    int err;     // output parameters
    void *data;
} AsyncData;

DBT *dbt_set(char *buf, u_int32_t flags = 0)
{
    DBT *dbt = (DBT*) malloc(sizeof(DBT));
    memset(dbt, 0, sizeof(DBT));
    dbt->flags = flags;
    if (buf) {
        dbt->size = (u_int32_t) strlen(buf);
        dbt->data = malloc(dbt->size);
        memcpy(dbt->data, buf, dbt->size);
    } else if (flags & DB_DBT_USERMEM) {
        dbt->data = malloc(BUFFER_LENGTH);
        dbt->ulen = BUFFER_LENGTH;
    }
    return dbt;
}

uv_work_t* async_before(DB *db, DB_TXN *txn, DBC *cur, char *key, char *value, 
        const Local<Value> &cb, u_int32_t flags = 0, int query = 0) {
    uv_work_t *req = new uv_work_t;
    AsyncData *data = new AsyncData;
    data->callback = Persistent<Function>::New(Local<Function>::Cast(cb));
    data->db = db;
    data->txn = txn;
    data->cur = cur;
    data->flags = flags;
    data->key_dbt = 0;
    data->data_dbt = 0;
    data->key = 0;
    data->value = 0;
    if (query) {
        data->key_dbt = dbt_set(key, DB_DBT_MALLOC);
        data->data_dbt = dbt_set(value, value ? 0 : 
            (
            (flags & DB_MULTIPLE) || (flags & DB_MULTIPLE_KEY) ? 
            DB_DBT_USERMEM : DB_DBT_MALLOC
            ));
        data->key = (char *) data->key_dbt->data;
        data->value = (char *) data->data_dbt->data;
    }
    req->data = data;
    return req;
}

#define ASYNC_AFTER_HEAD \
        AsyncData *data = (AsyncData *) req->data; \
        Handle<Value> result = Undefined(); \
        Handle<Value> keyresult = Undefined();

#define ASYNC_AFTER_TAIL(argn) \
        Handle<Value> argv[] = { err_object(data->err), result, keyresult }; \
        TryCatch try_catch; \
        data->callback->Call(Context::GetCurrent()->Global(), argn, argv); \
        if (try_catch.HasCaught()) node::FatalException(try_catch); \
        data->callback.Dispose(); \
        delete data; \
        delete req;

void async_after(uv_work_t *req) {
    ASYNC_AFTER_HEAD;

    DBT *key_dbt = data->key_dbt;
    DBT *data_dbt = data->data_dbt;
    int argn = 1;

    if (key_dbt) {
        argn = 3;
        char *key = (char *) key_dbt->data;
        char *value = (char *) data_dbt->data;

        if (value) {
            if (data_dbt->flags & DB_DBT_USERMEM) { // get
                size_t retklen, retdlen;
                unsigned char *retkey, *retdata;
                Local<Array> array = Array::New();
                int i = 0;
                void *p;

		if (data->flags & DB_MULTIPLE) {
                    for (DB_MULTIPLE_INIT(p, data_dbt);; i++) {
                        DB_MULTIPLE_NEXT(p, data_dbt, retdata, retdlen);
                        if (p == NULL) break;
                        array->Set(i, String::New((char *) retdata, retdlen));
                    }
                } else if (data->flags & DB_MULTIPLE_KEY) {
                    for (DB_MULTIPLE_INIT(p, data_dbt);; i++) {
                        DB_MULTIPLE_KEY_NEXT(p, data_dbt, retkey, retklen, retdata, retdlen);
                        if (p == NULL) break;
                        Local<Array> kv = Array::New();
                        kv->Set(0, String::New((char *) retdata, retdlen));
                        kv->Set(1, String::New((char *) retkey, retklen));
                        array->Set(i, kv);
                    }
                }

                result = array;
            } else {
                result = String::New(value, data_dbt->size);
            }

            if (data->value && data->value != value) free(data->value);
            free(value);
        }
        if (key) {
            keyresult = String::New(key, key_dbt->size);
            if (data->key && data->key != key) free(data->key);
            free(key);
        }
        free(key_dbt);
        free(data_dbt);
    }

    ASYNC_AFTER_TAIL(argn);
}

/***
Exported library methods
-------------------------

To use bdbstore, first import the library using require.

    store = require('bdbstore')

Once the library is imported, bdbstore exposes the following
methods:

    env = store.createEnv()

The method calls the C API function db\_env\_create() to create
a Berkeley DB database environment. The method returns an
object with methods for operating on this new environment.  This new
environment is used as the currently active database environment.  The
library package supports the use of only one Berkeley DB environment
at a time, however.  This method takes no arguments.

    db = store.createDb()

The method calls the C API function db\_create() to create
a Berkeley DB database.  The method returns an an object with methods
for operating on the new database.  The library package supports the
manipulation of multiple databases at a time.  The currently active
database environment will be be passed to db\_create().  This method
takes no arguments.
*/

Handle<Value> _db_create(const Arguments& args) {
    CHECK_NUMARGS(0, 0);
    DB *db = NULL;
    db_create(&db, dbenv, 0);
    RETURN_OBJECT(db_object(db, NULL));
}

Handle<Value> _env_create(const Arguments& args) {
    CHECK_NUMARGS(0, 0);
    db_env_create(&dbenv, 0);
    RETURN_OBJECT(env_object());
}

/***
The error object
----------------

A zero return value from the Berkeley DB C API is represented by a
null.  Nonzero return values however are wrapped in a error object.
This object has two properties.  The 'message' property is a string
representation of the error, while the 'error' property is the
number returned by the API call.
*/

Handle<Value> err_object(int ret) {
    if (!ret) return Null();
    Local<Object> obj = Object::New();
    SET_VALUE(obj, "message", String::New(db_strerror(ret)));
    SET_VALUE(obj, "error", Number::New(ret));
    return obj;
}

/***
The options object
------------------

The library package uses a special options object to represent the
flags passed to the Berkeley DB C API.  A Berkeley DB flag is set
by adding an identically named property, excluding the DB\_ prefix,
with a truthy value to the options object.
*/

u_int32_t get_flags(Local<Value> obj, u_int32_t flags = 0) {
    if (!obj->IsObject()) return 0;
    Local<Object> target = obj->ToObject();
    IF_TRUE_SET_FLAG("after", DB_AFTER);
    IF_TRUE_SET_FLAG("append", DB_APPEND);
    IF_TRUE_SET_FLAG("auto_commit", DB_AUTO_COMMIT);
    IF_TRUE_SET_FLAG("before", DB_BEFORE);
    IF_TRUE_SET_FLAG("cdb_alldb", DB_CDB_ALLDB);
    IF_TRUE_SET_FLAG("chksum", DB_CHKSUM);
    IF_TRUE_SET_FLAG("consume", DB_CONSUME);
    IF_TRUE_SET_FLAG("consume_wait", DB_CONSUME_WAIT);
    IF_TRUE_SET_FLAG("create", DB_CREATE);
    IF_TRUE_SET_FLAG("current", DB_CURRENT);
    IF_TRUE_SET_FLAG("cursor_bulk", DB_CURSOR_BULK);
    IF_TRUE_SET_FLAG("direct_db", DB_DIRECT_DB);
    IF_TRUE_SET_FLAG("dsync_db", DB_DSYNC_DB);
    IF_TRUE_SET_FLAG("dup", DB_DUP);  // btree, hash
    IF_TRUE_SET_FLAG("dupsort", DB_DUPSORT); // btree, hash
    IF_TRUE_SET_FLAG("encrypt", DB_ENCRYPT);
    IF_TRUE_SET_FLAG("excl", DB_EXCL);
    IF_TRUE_SET_FLAG("failchk", DB_FAILCHK);
    IF_TRUE_SET_FLAG("first", DB_FIRST);
    IF_TRUE_SET_FLAG("get_both", DB_GET_BOTH);
    IF_TRUE_SET_FLAG("get_both_range", DB_GET_BOTH_RANGE);
    IF_TRUE_SET_FLAG("get_recno", DB_GET_RECNO);
    IF_TRUE_SET_FLAG("hotbackup_in_progress", DB_HOTBACKUP_IN_PROGRESS);
    IF_TRUE_SET_FLAG("ignore_lease", DB_IGNORE_LEASE);
    IF_TRUE_SET_FLAG("init_cdb", DB_INIT_CDB);
    IF_TRUE_SET_FLAG("init_lock", DB_INIT_LOCK);
    IF_TRUE_SET_FLAG("init_log", DB_INIT_LOG);
    IF_TRUE_SET_FLAG("init_mpool", DB_INIT_MPOOL);
    IF_TRUE_SET_FLAG("init_rep", DB_INIT_REP);
    IF_TRUE_SET_FLAG("init_txn", DB_INIT_TXN);
    IF_TRUE_SET_FLAG("inorder", DB_INORDER);     // queue
    IF_TRUE_SET_FLAG("join_item", DB_JOIN_ITEM);
    IF_TRUE_SET_FLAG("keyfirst", DB_KEYFIRST);
    IF_TRUE_SET_FLAG("keylast", DB_KEYLAST);
    IF_TRUE_SET_FLAG("last", DB_LAST);
    IF_TRUE_SET_FLAG("lockdown", DB_LOCKDOWN);
    IF_TRUE_SET_FLAG("multiple", DB_MULTIPLE);
    IF_TRUE_SET_FLAG("multiple_key", DB_MULTIPLE_KEY);
    IF_TRUE_SET_FLAG("multiversion", DB_MULTIVERSION);
    IF_TRUE_SET_FLAG("next", DB_NEXT);
    IF_TRUE_SET_FLAG("next_dup", DB_NEXT_DUP);
    IF_TRUE_SET_FLAG("next_nodup", DB_NEXT_NODUP);
    IF_TRUE_SET_FLAG("nodupdata", DB_NODUPDATA);
    IF_TRUE_SET_FLAG("nolocking", DB_NOLOCKING);
    IF_TRUE_SET_FLAG("nommap", DB_NOMMAP);
    IF_TRUE_SET_FLAG("nooverwrite", DB_NOOVERWRITE);
    IF_TRUE_SET_FLAG("nopanic", DB_NOPANIC);
    IF_TRUE_SET_FLAG("overwrite", DB_OVERWRITE);
    IF_TRUE_SET_FLAG("overwrite_dup", DB_OVERWRITE_DUP);
    IF_TRUE_SET_FLAG("panic_environment", DB_PANIC_ENVIRONMENT);
    IF_TRUE_SET_FLAG("prev", DB_PREV);
    IF_TRUE_SET_FLAG("prev_dup", DB_PREV_DUP);
    IF_TRUE_SET_FLAG("prev_nodup", DB_PREV_NODUP);
    IF_TRUE_SET_FLAG("private", DB_PRIVATE);
    IF_TRUE_SET_FLAG("rdonly", DB_RDONLY);
    IF_TRUE_SET_FLAG("read_committed", DB_READ_COMMITTED);
    IF_TRUE_SET_FLAG("read_uncommitted", DB_READ_UNCOMMITTED);
    IF_TRUE_SET_FLAG("recnum", DB_RECNUM);       // btree
    IF_TRUE_SET_FLAG("recover", DB_RECOVER);
    IF_TRUE_SET_FLAG("recover_fatal", DB_RECOVER_FATAL);
    IF_TRUE_SET_FLAG("region_init", DB_REGION_INIT);
    IF_TRUE_SET_FLAG("register", DB_REGISTER);
    IF_TRUE_SET_FLAG("renumber", DB_RENUMBER);   // recno
    IF_TRUE_SET_FLAG("revsplitoff", DB_REVSPLITOFF); // btree, hash
    IF_TRUE_SET_FLAG("rmw", DB_RMW);
    IF_TRUE_SET_FLAG("set", DB_SET);
    IF_TRUE_SET_FLAG("set_lock_timeout", DB_SET_LOCK_TIMEOUT);
    IF_TRUE_SET_FLAG("set_range", DB_SET_RANGE);
    IF_TRUE_SET_FLAG("set_recno", DB_SET_RECNO);
    IF_TRUE_SET_FLAG("set_reg_timeout", DB_SET_REG_TIMEOUT);
    IF_TRUE_SET_FLAG("set_txn_timeout", DB_SET_TXN_TIMEOUT);
    IF_TRUE_SET_FLAG("snapshot", DB_SNAPSHOT);   // recno
    IF_TRUE_SET_FLAG("system_mem", DB_SYSTEM_MEM);
    IF_TRUE_SET_FLAG("thread", DB_THREAD);
    IF_TRUE_SET_FLAG("time_notgranted", DB_TIME_NOTGRANTED);
    IF_TRUE_SET_FLAG("truncate", DB_TRUNCATE);
    IF_TRUE_SET_FLAG("txn_bulk", DB_TXN_BULK);
    IF_TRUE_SET_FLAG("txn_nosync", DB_TXN_NOSYNC);
    IF_TRUE_SET_FLAG("txn_not_durable", DB_TXN_NOT_DURABLE);
    IF_TRUE_SET_FLAG("txn_nowait", DB_TXN_NOWAIT);
    IF_TRUE_SET_FLAG("txn_snapshot", DB_TXN_SNAPSHOT);
    IF_TRUE_SET_FLAG("txn_sync", DB_TXN_SYNC);
    IF_TRUE_SET_FLAG("txn_wait", DB_TXN_WAIT);
    IF_TRUE_SET_FLAG("txn_write_nosync", DB_TXN_WRITE_NOSYNC);
    IF_TRUE_SET_FLAG("use_environ", DB_USE_ENVIRON);
    IF_TRUE_SET_FLAG("use_environ_root", DB_USE_ENVIRON_ROOT);
    IF_TRUE_SET_FLAG("writecursor", DB_WRITECURSOR);
    IF_TRUE_SET_FLAG("yieldcpu", DB_YIELDCPU);
    return flags;    
}


/***
The environment object
------------------

The environment object wraps the environment handle returned by the
Berkeley DB C API.  The object's methods for manipulating this handle
are as follows:
*/

/***
    env.begin([options], callback)

The method begin calls DB\_ENV->txn\_begin().  Once the transaction
begins the callback is called with two arguments, the first being the
error object while the second is the transaction object.  The
transaction object has methods for committing or aborting the 
started transaction.  It also has a method for wrapping a database
object in the transaction.  This method returns undefined.
*/

Handle<Value> _env_txn_begin(const Arguments& args) {
    struct f {
        static void async_abort(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            DB_TXN* txn;
            data->err = dbenv->txn_begin(dbenv, data->txn, &txn, data->flags);
            data->data = txn;
        };
        static void async_after(uv_work_t *req) {
            ASYNC_AFTER_HEAD;
            result = txn_object((DB_TXN*) data->data);
            ASYNC_AFTER_TAIL(2);
        }
    };
    CHECK_NUMARGS(1, 2);
    CHECK_CALLBACK;
    GET_DBTXN;
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, txn, NULL, 0, 0, 
            args[args.Length() - 1], // callback
            args.Length() > 1 ? get_flags(args[0]) : 0), 
        f::async_abort, 
        (uv_after_work_cb) f::async_after);
    RETURN_UNDEFINED;
}

/***
    err = env.flags(options, [onoff])

The method flags calls DB\_ENV->set\_flags().  The onoff argument is
optional, defaulting to 1.  This method returns null or an error object.
*/

Handle<Value> _env_set_flags(const Arguments& args) {
    CHECK_NUMARGS(1, 2);
    int ret = dbenv->set_flags(dbenv, get_flags(args[0]), args.Length() == 1 ? 1 : args[1]->Uint32Value());
    RETURN_ERR;
}

/***
    err = env.close()

The method calls DB\_ENV->close(). 
This method returns null or an error object.
*/

Handle<Value> _env_close(const Arguments& args) {
    CHECK_NUMARGS(0, 0);
    int ret = dbenv->close(dbenv, 0);
    RETURN_ERR;
}

/***
    err = env.open(dbhome, options, [mode])

The method calls DB\_ENV->open().  If dbhome is set to null, NULL
will be passed to DB\_ENV->open() for dbhome.
This method returns null or an error object.
*/

Handle<Value> _env_open(const Arguments& args) {
    CHECK_NUMARGS(2, 3);
    String::Utf8Value dbhome(args[0]);
    u_int32_t flags = get_flags(args[1]);
    int ret = dbenv->open(dbenv, args[0]->IsNull() ? 0 : *dbhome, flags, 
        args.Length() > 2 ? args[2]->Uint32Value() : 0);
    RETURN_ERR;
}

Local<Object> env_object() {
    Local<Object> target = Object::New();
    SET_METHOD("flags", _env_set_flags);        // returns err
    SET_METHOD("open", _env_open);              // returns err
    SET_METHOD("close", _env_close);            // returns err
    SET_METHOD("begin", _env_txn_begin);        // async
    return target;
}


/***
The database object
-----------------------------------

The database object wraps the database handle returned by the Berkeley
DB C API.  The object's methods for manipulating this handle are as
follows:
*/

/**
    db.cursor([options], callback)

The method calls DB->cursor() to create a cursor for the database.
The calllback is called with null or an error object from the call as
the first argument.  The second argument passed is a cursor object 
which has methods for manipulating the created cursor.  This method
returns undefined.
*/
Handle<Value> _db_cursor(const Arguments& args) {
    struct f {
        static void async_begin(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            DBC *cur = NULL;
            data->err = data->db->cursor(data->db, data->txn, &cur, data->flags);
            data->data = cur;
        };
        static void async_after(uv_work_t *req) {
            ASYNC_AFTER_HEAD;
            result = cursor_object((DBC*) data->data);
            ASYNC_AFTER_TAIL(2);
        }
    };
    CHECK_NUMARGS(1, 2);
    CHECK_CALLBACK;
    GET_DBTXN;
    GET_DB;
    uv_queue_work(uv_default_loop(), 
        async_before(db, txn, NULL, 0, 0, 
            args[args.Length() - 1], // callback
            args.Length() > 1 ? get_flags(args[0]) : 0),
        f::async_begin, 
        (uv_after_work_cb) f::async_after);
    RETURN_UNDEFINED;
}

/**
    db.get(key, [options], callback)

The method calls DB->get().  The callback is called with a null or an
error object returned from the call as the first argument.  The second
argument is the found value (or array of values if the 'multiple'
option is set) for the key.  The third argument is the key.  The
function returns undefined.
*/
Handle<Value> _db_get(const Arguments& args) {
    struct f {
        static void async_get(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->db->get(data->db, data->txn, data->key_dbt, data->data_dbt, data->flags);
        }
    };
    CHECK_NUMARGS(2, 3);
    CHECK_CALLBACK;
    GET_DBTXN;
    GET_DB;
    String::Utf8Value key(args[0]);
    uv_queue_work(uv_default_loop(), 
        async_before(db, txn, NULL, *key, 0, 
            args[args.Length() - 1], // callback
            args.Length() > 2 ? get_flags(args[1]) : 0, 1), 
        f::async_get, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/**
    db.del(key, [options], callback)

The method calls DB->del() to delete key-values from the database.
Multiple deletes in a single call are unsupported at the moment. The
callback is called with a null or an error object returned from the
call as the first argument.  The second argument is undefined.  The
third argument is the key passed.  This method returns undefined.
*/
Handle<Value> _db_del(const Arguments& args) {
    struct f {
        static void async_del(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->db->del(data->db, data->txn, data->key_dbt, data->flags);
        }
    };
    CHECK_NUMARGS(2, 3);
    CHECK_CALLBACK;
    GET_DBTXN;
    GET_DB;
    String::Utf8Value key(args[0]);
    uv_queue_work(uv_default_loop(), 
        async_before(db, txn, NULL, *key, 0, 
            args[args.Length() - 1], // callback
            args.Length() > 2 ? get_flags(args[1]) : 0, 1), 
        f::async_del, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/**
    db.put(key, value, [options], callback)
 
The method calls DB->put() to put the given key-value pair into the
database.  Multiple puts in single call are unsupported at the moment.
The callback is called with a null or an error object returned from
the call as the first argument.  The second argument is the value
passed.  The third argument is the key passed.  This method returns
undefined.
*/
Handle<Value> _db_put(const Arguments& args) {
    struct f {
        static void async_put(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->db->put(data->db, data->txn, data->key_dbt, data->data_dbt, data->flags);
        }
    };
    CHECK_NUMARGS(3, 4);
    CHECK_CALLBACK;
    GET_DBTXN;
    GET_DB;
    String::Utf8Value key(args[0]);
    String::Utf8Value value(args[1]);
    uv_queue_work(uv_default_loop(), 
        async_before(db, txn, NULL, *key, *value, 
            args[args.Length() - 1], // callback
            args.Length() > 3 ? get_flags(args[2]) : 0, 1), 
        f::async_put, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/**
    err = db.flags(options)

The method calls DB->set\_flags() to set database specific options.
Null is returned if no error occurs otherwise an error object is
returned.
*/
Handle<Value> _db_set_flags(const Arguments& args) {
    CHECK_NUMARGS(1, 1);
    GET_DB;
    int ret = db->set_flags(db, get_flags(args[0]));
    RETURN_ERR;
}

/**
    err = db.close()

The method calls DB->close() to close the database object.  
This method returns null or an error object.
*/
Handle<Value> _db_close(const Arguments& args) {
    CHECK_NUMARGS(0, 0);
    GET_DB;
    int ret = db->close(db, 0);
    RETURN_ERR;
}

/**
    err = db.open(filename, [options, [mode]])

The method opens a database by calling DB->open().  The access method
for the new database can be changed from Btrees by setting the hash,
heap, recno, queue or unknown options properties to true.  Mode is the
file mode bits for the database file.  This method returns null or an
error object.
*/
Handle<Value> _db_open(const Arguments& args) {
    CHECK_NUMARGS(1, 3);
    GET_DBTXN;
    GET_DB;
    String::Utf8Value dbfile(args[0]);
    u_int32_t flags = 0;
    DBTYPE type = (DBTYPE) 0;
    if (args.Length() > 1 && args[1]->IsObject()) {
        Local<Object> obj = args[1]->ToObject();
        if (GET_BOOLEAN(obj, "hash")) type = DB_HASH;
        if (GET_BOOLEAN(obj, "heap")) type = DB_HEAP;
        if (GET_BOOLEAN(obj, "recno")) type = DB_RECNO;
        if (GET_BOOLEAN(obj, "queue")) type = DB_QUEUE;
        if (GET_BOOLEAN(obj, "unknown")) type = DB_UNKNOWN;
        flags = get_flags(args[1]);
    }
    if (!type) type = DB_BTREE;
    int ret = db->open(db, txn, *dbfile, NULL, type, flags, 
        args.Length() > 2 ? args[2]->Uint32Value() : 0);
    RETURN_ERR;
}

Local<Object> db_object(DB *db, DB_TXN *txn) {
    Local<Object> target = Object::New();
    SET_METHOD("cursor", _db_cursor);            // async (err, cursor obj)
    SET_METHOD("get", _db_get);                  // async (err, data)
    SET_METHOD("put", _db_put);                  // async (err)
    SET_METHOD("del", _db_del);                  // async (err)
    SET_METHOD("open", _db_open);                // returns err
    SET_METHOD("close", _db_close);              // returns err
    SET_METHOD("flags", _db_set_flags);          // returns err
    SET_EXTERNAL(target, "_db", db);
    if (txn) SET_EXTERNAL(target, "_txn", txn);
    return target;
}


/***
The cursor object
---------------------------------

The cursor object wraps the cursor handle returned by the Berkeley DB
C API.  The object's methods for manipulating this handle are as
follows:
*/

/***
    cur.put(key, value, options, callback)

Inserts or puts the passed key-value pair into the database using
DBCursor->put().  When the API is finished, the passed calllback
function is called with null or an error object as its first
parameter.  The second argument passed will be the actual value and
the third argument will be the actual key put into the database.  The
method returns undefined.
*/

Handle<Value> _cursor_put(const Arguments& args) {
    struct f {
        static void async_put(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->cur->put(data->cur, data->key_dbt, data->data_dbt, data->flags);
        };
    };
    CHECK_NUMARGS(4, 4);
    CHECK_CALLBACK;
    GET_DBCUR;
    String::Utf8Value key(args[0]);
    String::Utf8Value value(args[1]);
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, NULL, cur, args[0]->IsNull() ? 0 : *key, *value, 
            args[args.Length() - 1], // callback
            get_flags(args[2]), 
            1), 
        f::async_put, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/***
    cur.get([key], options, callback)

Gets the value for the referred to location using DBcursor->get().
The key parameter is optional.  If omitted or null is passed for it,
the API is called with a DBT *key field of NULL.  Once the API is
finished, the passed calllback function is called with null or an
error object as its first parameter.  The second argument passed will
be the value referred to and the third argument will be the key
referred to.  Multiple values are supported through the options
'multiple' and 'multiple\_key'.  This method returns undefined.
*/

Handle<Value> _cursor_get(const Arguments& args) {
    struct f {
        static void async_get(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->cur->get(data->cur, data->key_dbt, data->data_dbt, data->flags);
        };
    };
    CHECK_NUMARGS(2, 3);
    CHECK_CALLBACK;
    GET_DBCUR;
    String::Utf8Value key(args[0]);
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, NULL, cur, 
            args.Length() < 3 || args[0]->IsNull() ? 0 : *key, 0, 
            args[args.Length() - 1], // callback
            get_flags(args[args.Length() > 2 ? 1 : 0]), 
            1), 
        f::async_get, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/***
    cur.del([options], callback)

Deletes the key-value pair that the cursor refers to using
DBcursor->del().  When the API is finished, the passed calllback
function is called with null or an error object as its first
parameter.  This method returns undefined.
*/

Handle<Value> _cursor_del(const Arguments& args) {
    struct f {
        static void async_del(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->cur->del(data->cur, data->flags);
        };
    };
    CHECK_NUMARGS(1, 2);
    CHECK_CALLBACK;
    GET_DBCUR;
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, NULL, cur, 0, 0, 
            args[args.Length() - 1], // callback
            args.Length() > 1 ? get_flags(args[0]) : 0), 
        f::async_del, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/***
    cur.close(callback)

Closes and discards the cursor using the DBcursor->close() call.  The
passed callback function is then called with null or an error object
as its first parameter.  This method returns undefined.
*/

Handle<Value> _cursor_close(const Arguments& args) {
    struct f {
        static void async_del(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->cur->close(data->cur); 
        };
    };
    CHECK_NUMARGS(1, 1);
    CHECK_CALLBACK;
    GET_DBCUR;
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, NULL, cur, 0, 0, args[0]), 
        f::async_del, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

Local<Object> cursor_object(DBC *cur) {
    Local<Object> target = Object::New();
    SET_METHOD("close", _cursor_close);    // async (err)
    SET_METHOD("get", _cursor_get);        // async (err, data)
    SET_METHOD("put", _cursor_put);        // async (err)
    SET_METHOD("del", _cursor_del);        // async (err)
    SET_EXTERNAL(target, "_cur", cur);
    return target;
}

/***
The transaction object
--------------------------------------

The transaction object wraps the transaction handle returned by the
Berkeley DB C API.  The object's methods for manipulating this handle
are as follows:
*/

/**
    txn.commit(callback)

This method calls DB\_TXN->commit().  The passed callback is called
with one argument, a null or an error object returned from the commit call.
This method returns undefined.
*/

Handle<Value> _txn_commit(const Arguments& args) {
    struct f {
        static void async_commit(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->txn->commit(data->txn, 0);
        };
    };
    CHECK_NUMARGS(1, 1);
    CHECK_CALLBACK;
    GET_DBTXN;
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, txn, NULL, 0, 0, args[0]), 
        f::async_commit, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/**
    txn.abort(callback)

This method calls DB\_TXN->abort().  The passed callback is called
with one argument, null or an error object returned from the abort call.
The function returns undefined.
*/

Handle<Value> _txn_abort(const Arguments& args) {
    struct f {
        static void async_abort(uv_work_t *req) {
            AsyncData *data = (AsyncData *) req->data;
            data->err = data->txn->abort(data->txn);
        };
    };
    CHECK_NUMARGS(1, 1);
    CHECK_CALLBACK;
    GET_DBTXN;
    uv_queue_work(uv_default_loop(), 
        async_before(NULL, txn, NULL, 0, 0, args[0]), 
        f::async_abort, 
        (uv_after_work_cb) async_after);
    RETURN_UNDEFINED;
}

/**
    newdb = txn.wrap(db)

This method takes the passed database object and creates a new
database object encapsulating the transaction handle from the
transaction object.  Any database method calls off this newly created
database object will use this transaction handle when making Berkeley
DB API calls.
*/

Handle<Value> _txn_wrap(const Arguments& args) {
    CHECK_NUMARGS(1, 1);
    GET_DBTXN;
    DB *db = (DB*) GET_EXTERNAL(args[0]->ToObject(), "_db");
    HandleScope scope;
    return scope.Close(db_object(db, txn));
}

/**
    txn.begin([options], callback)

This method calls DB\_TXN->txn\_begin().  The new transaction will be
nested within the transaction object this method is called on.  The
passed callback is called with one argument, null or an error object
returned from the txn\_begin API call.  This method returns undefined.
*/

Local<Object> txn_object(DB_TXN *txn) {
    Local<Object> target = Object::New();
    SET_METHOD("commit", _txn_commit);       // async (err)
    SET_METHOD("abort", _txn_abort);         // async (err)
    SET_METHOD("wrap", _txn_wrap);           // returns new db object
    SET_METHOD("begin", _env_txn_begin);        // async
    SET_EXTERNAL(target, "_txn", txn);
    return target;
}


/////////////// addon initialization ////////////////

/***
Examples
--------

A simple key-value pair get and put example:

    var store = require('bdbstore');
    var db = store.createDb();
    db.open('store.db', { create: true });
    db.put('Bali', 'Denpasar', {}, function(err, value, key) {
        if (!err) console.log('put:', key, value)
        db.put('Bali', 'Denpasar', function(err, value, key) {
            if (!err) console.log('got:', key, value)
        });
    });

A simple cursor operation example:

    var store = require('bdbstore');
    var db = store.createDb();
    db.open('store.db', { create: true });
    db.put('Bali', 'Denpasar', function(err, value, key) {
        if (!err) console.log('put:', key, value)
        db.cursor(db, function(err, cur) {
            cur.get({ next: true }, function(err, value, key) {
                if (!err) console.log('got with a cursor:', key, value)
            });
        });
    });

A transaction operation example:

    var store = require('bdbstore');
    var env = store.createEnv();
    env.open('env', {
        create: true, 
        init_mpool: true,
        init_txn: true, 
        init_lock: true,
        init_log: true,
        thread: true,
    });
    var db = store.createDb();
    db.open('store.db', { create: true, auto_commit: true });
    env.begin(function(err, txn) {
        txn.wrap(db).put('Bali', 'Denpasar', function(err, value, key) {
            if (!err) console.log('put:', key, value)
            txn.commit(function(err) {
                if (!err) console.log('put committed')
                db.get('Bali', function(err, value, key) { 
                    if (!err) console.log('got:', key, value)
                });
            });
        });
    });

*/

void init(Handle<Object> target) {
    SET_METHOD("createEnv", _env_create);       // returns env object
    SET_METHOD("createDb", _db_create);         // returns db object
}

NODE_MODULE(bdbstore, init)


