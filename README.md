
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

The error object
----------------

A zero return value from the Berkeley DB C API is represented by a
null.  Nonzero return values however are wrapped in a error object.
This object has two properties.  The 'message' property is a string
representation of the error, while the 'error' property is the
number returned by the API call.

The options object
------------------

The library package uses a special options object to represent the
flags passed to the Berkeley DB C API.  A Berkeley DB flag is set
by adding an identically named property, excluding the DB\_ prefix,
with a truthy value to the options object.

The environment object
------------------

The environment object wraps the environment handle returned by the
Berkeley DB C API.  The object's methods for manipulating this handle
are as follows:

    env.begin([options], callback)

The method begin calls DB\_ENV->txn\_begin().  Once the transaction
begins the callback is called with two arguments, the first being the
error object while the second is the transaction object.  The
transaction object has methods for committing or aborting the 
started transaction.  It also has a method for wrapping a database
object in the transaction.  This method returns undefined.

    err = env.flags(options, [onoff])

The method flags calls DB\_ENV->set\_flags().  The onoff argument is
optional, defaulting to 1.  This method returns null or an error object.

    err = env.close()

The method calls DB\_ENV->close(). 
This method returns null or an error object.

    err = env.open(dbhome, options, [mode])

The method calls DB\_ENV->open().  If dbhome is set to null, NULL
will be passed to DB\_ENV->open() for dbhome.
This method returns null or an error object.

The database object
-----------------------------------

The database object wraps the database handle returned by the Berkeley
DB C API.  The object's methods for manipulating this handle are as
follows:

    db.cursor([options], callback)

The method calls DB->cursor() to create a cursor for the database.
The calllback is called with null or an error object from the call as
the first argument.  The second argument passed is a cursor object 
which has methods for manipulating the created cursor.  This method
returns undefined.

    db.get(key, [options], callback)

The method calls DB->get().  The callback is called with a null or an
error object returned from the call as the first argument.  The second
argument is the found value (or array of values if the 'multiple'
option is set) for the key.  The third argument is the key.  The
function returns undefined.

    db.del(key, [options], callback)

The method calls DB->del() to delete key-values from the database.
Multiple deletes in a single call are unsupported at the moment. The
callback is called with a null or an error object returned from the
call as the first argument.  The second argument is undefined.  The
third argument is the key passed.  This method returns undefined.

    db.put(key, value, [options], callback)
 
The method calls DB->put() to put the given key-value pair into the
database.  Multiple puts in single call are unsupported at the moment.
The callback is called with a null or an error object returned from
the call as the first argument.  The second argument is the value
passed.  The third argument is the key passed.  This method returns
undefined.

    err = db.flags(options)

The method calls DB->set\_flags() to set database specific options.
Null is returned if no error occurs otherwise an error object is
returned.

    err = db.close()

The method calls DB->close() to close the database object.  
This method returns null or an error object.

    err = db.open(filename, [options, [mode]])

The method opens a database by calling DB->open().  The access method
for the new database can be changed from Btrees by setting the hash,
heap, recno, queue or unknown options properties to true.  Mode is the
file mode bits for the database file.  This method returns null or an
error object.

The cursor object
---------------------------------

The cursor object wraps the cursor handle returned by the Berkeley DB
C API.  The object's methods for manipulating this handle are as
follows:

    cur.put(key, value, options, callback)

Inserts or puts the passed key-value pair into the database using
DBCursor->put().  When the API is finished, the passed calllback
function is called with null or an error object as its first
parameter.  The second argument passed will be the actual value and
the third argument will be the actual key put into the database.  The
method returns undefined.

    cur.get([key], options, callback)

Gets the value for the referred to location using DBcursor->get().
The key parameter is optional.  If omitted or null is passed for it,
the API is called with a DBT *key field of NULL.  Once the API is
finished, the passed calllback function is called with null or an
error object as its first parameter.  The second argument passed will
be the value referred to and the third argument will be the key
referred to.  Multiple values are supported through the options
'multiple' and 'multiple\_key'.  This method returns undefined.

    cur.del([options], callback)

Deletes the key-value pair that the cursor refers to using
DBcursor->del().  When the API is finished, the passed calllback
function is called with null or an error object as its first
parameter.  This method returns undefined.

    cur.close(callback)

Closes and discards the cursor using the DBcursor->close() call.  The
passed callback function is then called with null or an error object
as its first parameter.  This method returns undefined.

The transaction object
--------------------------------------

The transaction object wraps the transaction handle returned by the
Berkeley DB C API.  The object's methods for manipulating this handle
are as follows:

    txn.commit(callback)

This method calls DB\_TXN->commit().  The passed callback is called
with one argument, a null or an error object returned from the commit call.
This method returns undefined.

    txn.abort(callback)

This method calls DB\_TXN->abort().  The passed callback is called
with one argument, null or an error object returned from the abort call.
The function returns undefined.

    newdb = txn.wrap(db)

This method takes the passed database object and creates a new
database object encapsulating the transaction handle from the
transaction object.  Any database method calls off this newly created
database object will use this transaction handle when making Berkeley
DB API calls.

    txn.begin([options], callback)

This method calls DB\_ENV->txn\_begin().  The new transaction will be
nested within the transaction object this method is called on.  The
passed callback is called with one argument, null or an error object
returned from the txn\_begin API call.  This method returns undefined.

Examples
--------

A simple key-value pair get and put example:

    var store = require('bdbstore');
    var db = store.createDb();
    db.open('store.db', { create: true });
    db.put('Bali', 'Denpasar', function(err, value, key) {
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

