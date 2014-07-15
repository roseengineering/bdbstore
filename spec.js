
var store = require('./index');
var async = require('./async');

// testing routines
///////////////////////////////////////

function doput(db, callback, obj) {
    async.series([
        function(cb) { db.put('Bali', 'Denpasar', obj, cb) },
        function(cb) { db.put('Java', 'Pasuruan', obj, cb) },
        function(cb) { db.put('Java', 'Bandung', obj, cb) },
        function(cb) { db.put('Java', 'Cimahi', obj, cb) },
    ], callback);
};

function doget(db, callback, obj) {
    async.series([
        function(cb) { db.get('Bali', obj, cb) },
        function(cb) { db.get('Java', obj, cb) },
    ], callback);
};

// specs
//////////////////////////////////////

// to do:
// mutliple_key (for cursor_get)

exports["should create a database"] = function (test) {
    console.log('with no environment');
    test.throws(function() { store.createDb(1) });
    db = store.createDb();
    test.ok(db);
    db.close();
    test.done();
};

exports["should open a database"] = function (test) {
    var db;
    db = store.createDb();
    test.throws(function() { db.open() });
    test.throws(function() { db.open(1,2,3,4) });
    var err = db.open("env/010.db", { create: true }, 0);
    test.ok(!err);
    db.close();

    db = store.createDb();
    var err = db.open("env/010.db", {}); 
    test.ok(!err);
    db.close();

    db = store.createDb();
    var err = db.open("env/011.db", { create: true }, 0600);
    test.ok(!err);
    db.close();

    test.done();
};

exports["should close a database"] = function (test) {
    var db = store.createDb();
    db.open("env/015.db", { create: true });
    test.throws(function() { db.close(1) });
    var err = db.close();
    test.ok(!err);
    test.done();
};

exports["should put data"] = function (test) {
    var db = store.createDb();
    db.open("env/020.db", { create: true });
    test.throws(function() { db.put() });
    test.throws(function() { db.put(1) });
    test.throws(function() { db.put(1,2) });
    test.throws(function() { db.put(1,2,3,4,5) });
    db.put('Bali', 'Denpasar', {}, function(err, res) {
        test.ok(!err);
        db.put('Bali', 'Denpasar', function(err, res) {
            test.ok(!err);
            db.close();
            test.done();
        });
    });
};

exports["should delete data"] = function (test) {
    var db = store.createDb();
    db.open("env/025.db", { create: true });
    test.throws(function() { db.del() });
    test.throws(function() { db.del(1) });
    test.throws(function() { db.del(1,2,3,4) });
    doput(db, function(err, res) {
        db.del("Bali", function(err, res) {
            test.ok(!err);
            db.del("Bali", {}, function(err, res) {
                test.equal(err.error, -30988);  // NOTFOUND
                db.close();
                test.done();
            })
        })
    });
};

exports["should get data"] = function (test) {
    var db = store.createDb();
    db.open("env/030.db", { create: true });
    test.throws(function() { db.get() });
    test.throws(function() { db.get(1) });
    test.throws(function() { db.get(1,2,3,4) });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0], "Denpasar");
            test.equal(res[1][0], "Cimahi");
            db.get("Bali", function(err, res) {
                test.ok(!err);
                db.get("Jamaica", {}, function(err, res) {
                    test.equal(err.error, -30988);  // NOTFOUND
                    db.close();
                    test.done();
                });
            });
        });
    });
};

exports["should get data with multiple flag set"] = function (test) {
    var db = store.createDb();
    db.open("env/040.db", { create: true });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0], "Denpasar");
            test.equal(res[1][0], "Cimahi");
            db.close();
            test.done();
        }, { multiple: true });
    });
};

exports["should get data with dup and multiple flags set"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/050.db", { create: true });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0].length, 1);
            test.equal(res[1][0].length, 3);
            test.equal(res[0][0][0], "Denpasar");
            test.equal(res[1][0][0], "Pasuruan");  // not sorted
            db.close();
            test.done();
        }, { multiple: true });
    });
};

exports["should get data with dup flag set"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/060.db", { create: true });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0], "Denpasar");
            test.equal(res[1][0], "Pasuruan");  // not sorted, gets first dup
            db.close();
            test.done();
        });
    });
};

exports["should get data with dupsort and multiple flags set"] = function (test) {
    var db = store.createDb();
    db.flags({ dupsort: true });
    db.open("env/070.db", { create: true });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0].length, 1);
            test.equal(res[1][0].length, 3);
            test.equal(res[0][0][0], "Denpasar");
            test.equal(res[1][0][0], "Bandung");  // sorted
            db.close();
            test.done();
        }, { multiple: true });
    });
};

exports["should get data with dupsort flag set"] = function (test) {
    var db = store.createDb();
    db.flags({ dupsort: true });
    db.open("env/080.db", { create: true });
    doput(db, function(err, res) {
        doget(db, function(err, res) {
            test.ok(!err);
            test.equal(res.length, 2);
            test.equal(res[0][0], "Denpasar");
            test.equal(res[1][0], "Bandung");  // sorted, gets first dup
            db.close();
            test.done();
        });
    });
};

exports["should open a cursor"] = function (test) {
    var db = store.createDb();
    db.open("env/090.db", { create: true });
    test.throws(function() { db.cursor(); });
    test.throws(function() { db.cursor(1); });
    test.throws(function() { db.cursor(1,2); });
    test.throws(function() { db.cursor(1,2,3); });
    doput(db, function(err, res) {
        db.cursor(function(err, res) {
            test.ok(!err);
            db.cursor({}, function(err, res) {
                test.ok(!err);
                test.ok(res.get);
                test.ok(res.put);
                test.ok(res.del);
                test.ok(res.close);
                db.close();
                test.done();
            });
        });
    });
};

exports["should close a cursor"] = function (test) {
    var db = store.createDb();
    db.open("env/095.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(function(err, cur) {
	    cur.close(function(err, res) {
                test.ok(!err);
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should move to first record"] = function (test) {
    var db = store.createDb();
    db.open("env/100.db", { create: true });
    doput(db, function(err, res) {
        async.waterfall([
            function(cb) { db.cursor(cb) },
            function(cur, cb) { cur.get({ first: true }, cb) }
        ], function(err, value, key) {
            test.ok(!err);
            test.equal(value, 'Denpasar');
            test.equal(key, 'Bali');
            db.close();
            test.done();
        });
    });
};

exports["cursor should move to next record"] = function (test) {
    var db = store.createDb();
    db.open("env/100.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should walk through dups"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/110.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
            ], function(err, res) {
                test.ok(!err);
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should walk through dups with multiple_key flag set"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/115.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next: true, multiple_key: true }, cb) },
            ], function(err, res) {
                test.ok(!err);
                test.equal(res[0][1], 'Bali');
                res = res[0][0];
                test.equal(res[0][0], 'Denpasar');
                test.equal(res[1][0], 'Pasuruan');
                test.equal(res[2][0], 'Bandung');
                test.equal(res[3][0], 'Cimahi');
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should walk backwards through dups"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/120.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            test.throws(function() { cur.get(1); });
            test.throws(function() { cur.get(1,2,3,4); });
            async.series([
                function(cb) { cur.get(null, { prev: true }, cb) },
                function(cb) { cur.get(null, { prev: true }, cb) },
                function(cb) { cur.get(null, { prev: true }, cb) },
                function(cb) { cur.get(null, { prev: true }, cb) },
            ], function(err, res) {
                test.ok(!err);
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should skip over dups"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/130.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next_nodup: true }, cb) },
                function(cb) { cur.get({ next_nodup: true }, cb) },
                function(cb) { cur.get({ next_nodup: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                test.equal(res[0][0], "Denpasar");
                test.equal(res[1][0], "Pasuruan");  // not sorted
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should skip over sorted dups"] = function (test) {
    var db = store.createDb();
    db.flags({ dupsort: true });
    db.open("env/140.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next_nodup: true }, cb) },
                function(cb) { cur.get({ next_nodup: true }, cb) },
                function(cb) { cur.get({ next_nodup: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                test.equal(res[0][0], "Denpasar");
                test.equal(res[1][0], "Bandung");  // not sorted
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should walk through dups with multiple flag set"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/150.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ multiple: true, next: true }, cb) },
                function(cb) { cur.get({ multiple: true, next: true }, cb) },
                function(cb) { cur.get({ multiple: true, next: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                test.equal(res[0][0][0], "Denpasar");
                test.equal(res[1][0][0], "Pasuruan");  // not sorted
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should delete a record"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/160.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.del(cb) },
                function(cb) { cur.get({ first: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                test.equal(res.length, 7);
                db.close();
                test.done();
            });
        });
    });
};

exports["cursor should delete the first record"] = function (test) {
    var db = store.createDb();
    db.flags({ dup: true });
    db.open("env/170.db", { create: true });
    doput(db, function(err, res) {
        db.cursor(db, function(err, cur) {
            async.series([
                function(cb) { cur.get({ first: true }, cb) }, // set before deleting
                function(cb) { cur.del(cb) }, 
                function(cb) { cur.get({ first: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
                function(cb) { cur.get({ next: true }, cb) },
            ], function(err, res) {
                test.equal(err.error, -30988);
                test.equal(res.length, 6);
                db.close();
                test.done();
            });
        });
    });
};

exports["should open a minimal non-locking environment"] = function (test) {
    console.log('\nwith an environment');
    var err, env = store.createEnv();
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true
    }, 0);
    test.ok(!err);
    err = env.close();
    test.ok(!err);
    test.done();
};

exports["should open a concurrent data store environment"] = function (test) {
    var env = store.createEnv();
    var err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_cdb: true, 
        thread: true
    });
    test.ok(!err);
    err = env.close();
    test.ok(!err);
    test.done();
};

exports["should open a transaction data store"] = function (test) {
    var env = store.createEnv();
    var err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    }, 0600);
    test.ok(!err);
    err = env.close();
    test.ok(!err);
    test.done();
};

exports["should begin a transaction"] = function (test) {
    var env = store.createEnv();
    var err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    env.begin(function(err, txn) {
        test.ok(!err);
        txn.commit(function(err) {
            test.ok(!err);
            err = env.close();
            test.ok(!err);
            test.done();
        });
    });
};

exports["should open a transaction supporting database"] = function (test) {
    var err, db, env = store.createEnv();
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    db = store.createDb();
    err = db.open("180.db", { create: true, auto_commit: true });
    test.ok(!err);
    err = db.close();
    test.ok(!err);

    env.begin(function(err, txn) {
        test.ok(!err);
        db = store.createDb();
        err = txn.wrap(db).open("190.db", { create: true });
        test.ok(!err);
        txn.commit(function(err) {
            test.ok(!err);
            err = db.close();
            test.ok(!err);
            err = env.close();
            test.ok(!err);
            test.done();
        });
    });
};


exports["should commit a change"] = function (test) {
    var err, db, env = store.createEnv();
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    db = store.createDb();
    db.open("190.db", { create: true, auto_commit: true });
    env.begin(function(err, txn) {
        test.ok(!err);
        txn.wrap(db).put('Bali', 'Denpasar', function(err) {
            test.ok(!err);
            txn.commit(function(err) {
                test.ok(!err);
                db.get('Bali', function() { 
                    test.ok(!err);
                    err = db.close();
                    err = env.close();
                    test.done();
                });
            });
        });
    });
};

exports["should abort a change"] = function (test) {
    var err, db, env = store.createEnv();
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    db = store.createDb();
    db.open("200.db", { create: true, auto_commit: true });
    env.begin(function(err, txn) {
        test.ok(!err);
        txn.wrap(db).put('Bali', 'Denpasar', function(err) {
            test.ok(!err);
            txn.abort(function(err) {
                test.ok(!err);
                db.get('Bali', function(err) { 
                    test.equal(err && err.error, -30988);
                    err = db.close();
                    err = env.close();
                    test.done();
                });
            });
        });
    });
};

exports["should open a database transactionally when auto_commit environment flag set"] = function (test) {
    var err, db, env = store.createEnv();
    err = env.flags({ auto_commit: true });
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    db = store.createDb();
    db.open("210.db", { create: true });
    env.begin(function(err, txn) {
        test.ok(!err);
        txn.wrap(db).put('Bali', 'Denpasar', function(err) {
            test.ok(!err);
            txn.commit(function(err) {
                test.ok(!err);
                db.get('Bali', function(err) { 
                    test.ok(!err);
                    err = db.close();
                    err = env.close();
                    test.done();
                });
            });
        });
    });
};

exports["should nest a transaction"] = function (test) {
    var err, db, env = store.createEnv();
    err = env.open('env', {
        private: true, 
        create: true, 
        init_mpool: true,
        init_txn: true, 
        thread: true,
        init_lock: true,
        init_log: true,
        recover: true,
    });
    db = store.createDb();
    db.open("190.db", { create: true, auto_commit: true });
    env.begin(function(err, outer) {
        test.ok(!err);
        outer.wrap(db).put('Bali', 'Denpasar', function(err) {
            test.ok(!err);
            outer.begin(function(err, inner) {
                test.ok(!err);
                inner.wrap(db).get('Bali', function() { 
                    test.ok(!err);
                    outer.commit(function(err) {
                        test.ok(!err);
                        err = db.close();
                        err = env.close();
                        test.done();
                    });
                });
            });
        });
    });
};

