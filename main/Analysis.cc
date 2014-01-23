#include <main/Analysis.hh>
#include <main/rewrite_util.hh>
#include <main/rewrite_main.hh>
#include <main/metadata_tables.hh>
#include <main/macro_util.hh>
#include <main/stored_procedures.hh>
#include <util/util.hh>

// FIXME: Wrong interfaces.
EncSet::EncSet(Analysis &a, FieldMeta * const fm) {
    TEST_TextMessageError(0 != fm->children.size(),
                         "FieldMeta has no children!");

    osl.clear();
    for (auto pair = fm->children.begin(); pair != fm->children.end();
         pair++) {
        OnionMeta *const om = (*pair).second.get();
        OnionMetaKey const &key = (*pair).first;
        osl[key.getValue()] = LevelFieldPair(a.getOnionLevel(*om), fm);
    }
}

EncSet::EncSet(const OLK & olk) {
    osl[olk.o] = LevelFieldPair(olk.l, olk.key);
}

EncSet
EncSet::intersect(const EncSet & es2) const
{
    OnionLevelFieldMap m;
    for (auto it2 = es2.osl.begin();
            it2 != es2.osl.end(); ++it2) {
        auto it = osl.find(it2->first);

        if (it != osl.end()) {
            FieldMeta * const fm = it->second.second;
            FieldMeta * const fm2 = it2->second.second;

            const onion o = it->first;
            const onion o2 = it2->first;

            assert(o == o2);

            const SECLEVEL sl =
                static_cast<SECLEVEL>(
                        min(static_cast<int>(it->second.first),
                            static_cast<int>(it2->second.first)));

            /*
             * FIXME: Each clause of this if statement should make sure
             * that it's OnionMeta actually has the SecLevel.
             */
            if (fm == NULL) {
                m[o] = LevelFieldPair(sl, fm2);
            } else if (fm2 == NULL) {
                m[it->first] = LevelFieldPair(sl, fm);
            } else {
                // This can succeed in three cases.
                // 1> Same field, so same key.
                // 2> Different fields, but SECLEVEL is PLAINVAL
                //    or DETJOIN so same key.
                // 3> Differt fields, and SECLEVEL is HOM so
                //    we will do computation client side if necessary.
                const OnionMeta * const om = fm->getOnionMeta(o);
                const OnionMeta * const om2 = fm2->getOnionMeta(o);
                // HACK: To determine if the keys are the same.
                if ((om->hasEncLayer(sl) && om2->hasEncLayer(sl)
                     && om->getLayer(sl)->doSerialize() ==
                        om2->getLayer(sl)->doSerialize())) {
                    m[o] = LevelFieldPair(sl, fm);
                }
            }
        }
    }
    return EncSet(m);
}

std::ostream&
operator<<(std::ostream &out, const EncSet &es)
{
    if (es.osl.size() == 0) {
        out << "empty encset";
    }
    for (auto it : es.osl) {
        out << "(onion " << it.first
            << ", level " << TypeText<SECLEVEL>::toText(it.second.first)
            << ", field `" << (it.second.second == NULL ? "*" : it.second.second->fname) << "`"
            << ") ";
    }
    return out;
}


OLK
EncSet::chooseOne() const
{
    // Order of selection is encoded in this array.
    // The onions appearing earlier are the more preferred ones.
    static const onion onion_order[] = {
        oDET,
        oOPE,
        oAGG,
        oSWP,
        oPLAIN,
    };

    static size_t onion_size =
        sizeof(onion_order) / sizeof(onion_order[0]);
    for (size_t i = 0; i < onion_size; i++) {
        const onion o = onion_order[i];
        const auto it = osl.find(o);
        if (it != osl.end()) {
            if (SECLEVEL::INVALID == it->second.first) {
                continue;
            }
            if (0 == it->second.second
                && (it->second.first != SECLEVEL::PLAINVAL
                    && o != oPLAIN)) {
                /*
                 * If no key, skip this OLK.
                 */
                continue;
            }

            return OLK(o, it->second.first, it->second.second);
        }
    }

    return OLK::invalidOLK();
}

bool
EncSet::contains(const OLK &olk) const {
    auto it = osl.find(olk.o);
    if (it == osl.end()) {
        return false;
    }
    if (it->second.first == olk.l) {
        return true;
    }
    return false;
}

bool
EncSet::hasSecLevel(SECLEVEL level) const
{
    for (auto it : osl) {
        if (it.second.first == level) {
            return true;
        }
    }

    return false;
}

SECLEVEL
EncSet::onionLevel(onion o) const
{
    for (auto it : osl) {
        if (it.first == o) {
            return it.second.first;
        }
    }

    assert(false);
}

bool
EncSet::available() const
{
    return OLK::isNotInvalid(this->chooseOne());
}

bool EncSet::single_crypted_and_or_plainvals() const
{
    unsigned int crypted = 0;
    unsigned int plain = 0;
    for (auto it : osl) {
        if (SECLEVEL::PLAINVAL == it.second.first) {
            ++plain;
        } else {
            ++crypted;
        }
    }

    return 1 >= crypted || plain > 0;
}

OLK EncSet::extract_singleton() const
{
    assert_s(singleton(), std::string("encset has size ") +
                            StringFromVal(osl.size()));
    const auto it = osl.begin();
    return OLK(it->first, it->second.first, it->second.second);
}

// needsSaltz must have consistent semantics.
static bool
needsSalt(SECLEVEL l)
{
    return l == SECLEVEL::RND;
}

bool
needsSalt(OLK olk)
{
    return olk.key && olk.key->getHasSalt() && needsSalt(olk.l);
}

bool
needsSalt(EncSet es)
{
    for (auto pair : es.osl) {
        OLK olk(pair.first, pair.second.first, pair.second.second);
        if (needsSalt(olk)) {
            return true;
        }
    }

    return false;
}

std::ostream&
operator<<(std::ostream &out, const reason &r)
{
    out << r.item << " PRODUCES encset " << r.encset << std::endl
        << " BECAUSE " << r.why << std::endl;

    return out;
}


/*
void
RewritePlan::restrict(const EncSet & es) {
    es_out = es_out.intersect(es);
    assert_s(!es_out.empty(), "after restrict rewrite plan has empty encset");

    if (plan.size()) { //node has children
        for (auto pair = plan.begin(); pair != plan.end(); pair++) {
            if (!es.contains(pair->first)) {
            plan.erase(pair);
            }
        }
    }
}
*/

std::ostream&
operator<<(std::ostream &out, const RewritePlan * const rp)
{
    if (!rp) {
        out << "NULL RewritePlan";
        return out;
    }

    out << " RewritePlan: \n---> out encset " << rp->es_out << "\n---> reason " << rp->r << "\n";

    return out;
}

bool
lowLevelGetCurrentDatabase(const std::unique_ptr<Connect> &c,
                           std::string *const out_db)
{
    const std::string query = "SELECT DATABASE();";
    std::unique_ptr<DBResult> db_res;
    RFIF(c->execute(query, &db_res));
    assert(1 == mysql_num_rows(db_res->n));

    const MYSQL_ROW row = mysql_fetch_row(db_res->n);
    const unsigned long *const l = mysql_fetch_lengths(db_res->n);
    assert(l != NULL);

    assert((0 == l[0]) == (NULL == row[0]));
    *out_db = std::string(row[0], l[0]);
    if (out_db->size() == 0) {
        return true;
    }

    return true;
}

bool
lowLevelSetCurrentDatabase(const std::unique_ptr<Connect> &c,
                           const std::string &db)
{
    // Use HACK to get this connection to use NULL as default DB.
    if (db.size() == 0) {
        const std::string random_name = getpRandomName();
        RFIF(c->execute("CREATE DATABASE " + random_name + ";"));
        RFIF(c->execute("USE " + random_name + ";"));
        RFIF(c->execute("DROP DATABASE " + random_name + ";"));

        return true;
    }

    const std::string query = "USE " + quoteText(db) + ";";
    RFIF(c->execute(query));

    return true;
}

static void
dropAll(const std::unique_ptr<Connect> &conn)
{
    for (const udf_func * const u: udf_list) {
        const std::string s =
            "DROP FUNCTION IF EXISTS " + convert_lex_str(u->name) + ";";
        assert_s(conn->execute(s), s);
    }
}

std::vector<std::string>
getAllUDFs()
{
    std::vector<std::string> udfs;
    for (const udf_func * const u: udf_list) {
        std::stringstream ss;
        ss << "CREATE ";
        if (u->type == UDFTYPE_AGGREGATE) ss << "AGGREGATE ";
        ss << "FUNCTION " << u->name.str << " RETURNS ";
        switch (u->returns) {
            case INT_RESULT:    ss << "INTEGER"; break;
            case STRING_RESULT: ss << "STRING";  break;
            default:            thrower() << "unknown return " << u->returns;
        }
        ss << " SONAME 'edb.so';";
        udfs.push_back(ss.str());
    }

    return udfs;
}

static void
createAll(const std::unique_ptr<Connect> &conn)
{
    auto udfs = getAllUDFs();
    for (auto it : udfs) {
        assert_s(conn->execute(it), it);
    }
}

static void
loadUDFs(const std::unique_ptr<Connect> &conn) {
    const std::string udf_db = "cryptdb_udf";
    assert_s(conn->execute("DROP DATABASE IF EXISTS " + udf_db), "cannot drop db for udfs even with 'if exists'");
    assert_s(conn->execute("CREATE DATABASE " + udf_db), "cannot create db for udfs");

    std::string saved_db;
    assert(lowLevelGetCurrentDatabase(conn, &saved_db));
    assert(lowLevelSetCurrentDatabase(conn, udf_db));
    dropAll(conn);
    createAll(conn);
    assert(lowLevelSetCurrentDatabase(conn, saved_db));

    LOG(cdb_v) << "Loaded CryptDB's UDFs.";
}

static bool
synchronizeDatabases(const std::unique_ptr<Connect> &conn,
                     const std::unique_ptr<Connect> &e_conn)
{
    std::string current_db;
    RFIF(lowLevelGetCurrentDatabase(conn, &current_db));
    RFIF(lowLevelSetCurrentDatabase(e_conn, current_db));

    return true;
}

SharedProxyState::SharedProxyState(ConnectionInfo ci,
                                   const std::string &embed_dir,
                                   const std::string &master_key,
                                   SECURITY_RATING default_sec_rating)
    : masterKey(std::unique_ptr<AES_KEY>(getKey(master_key))),
      embed_dir(embed_dir),
      mysql_dummy(SharedProxyState::db_init(embed_dir)), // HACK: Allows
                                                   // connections in init
                                                   // list.
      conn(new Connect(ci.server, ci.user, ci.passwd, ci.port)),
      default_sec_rating(default_sec_rating),
      cache(std::move(SchemaCache()))
{
    std::unique_ptr<Connect>
        init_e_conn(Connect::getEmbedded(embed_dir));
    assert(conn && init_e_conn);

    const std::string prefix = 
        getenv("CRYPTDB_NAME") ? getenv("CRYPTDB_NAME")
                               : "generic_prefix_";
    assert(MetaData::initialize(conn, init_e_conn, prefix));

    TEST_TextMessageError(synchronizeDatabases(conn, init_e_conn),
                          "Failed to synchronize embedded and remote"
                          " databases!");

    loadUDFs(conn);

    assert(loadStoredProcedures(conn));
}

SharedProxyState::~SharedProxyState()
{
    // mysql_library_end();
}

int
SharedProxyState::db_init(const std::string &embed_dir)
{
    init_mysql(embed_dir);
    return 1;
}

ProxyState::~ProxyState() {}

SECURITY_RATING
ProxyState::defaultSecurityRating() const
{
    return shared.defaultSecurityRating();
}

const std::unique_ptr<AES_KEY> &
ProxyState::getMasterKey() const
{
    return shared.getMasterKey();
}

const std::unique_ptr<Connect> &
ProxyState::getConn() const
{
    return shared.getConn();
}

const std::unique_ptr<Connect> &
ProxyState::getEConn() const
{
    return e_conn;
}

static void
embeddedTHDCleanup(THD *thd)
{
    thd->clear_data_list();
    --thread_count;
    // thd->unlink() is called in by THD destructor
    // > THD::~THD()
    //     ilink::~ilink()
    //       ilink::unlink()
    // free_root(thd->main_mem_root, 0) is called in THD::~THD
    delete thd;
}

void
ProxyState::safeCreateEmbeddedTHD()
{
    THD *thd = static_cast<THD *>(create_embedded_thd(0));
    assert(thd);
    thds.push_back(std::unique_ptr<THD,
                                   void (*)(THD *)>(thd,
                                       &embeddedTHDCleanup));
    return;
}

void ProxyState::dumpTHDs()
{
    for (auto it = thds.begin(); it != thds.end(); ++it) {
        it->release();
    }
    thds.clear();

    assert(0 == thds.size());
}

std::string Delta::tableNameFromType(TableType table_type) const
{
    switch (table_type) {
        case REGULAR_TABLE: {
            return MetaData::Table::metaObject();
        }
        case BLEEDING_TABLE: {
            return MetaData::Table::bleedingMetaObject();
        }
        default: {
            FAIL_TextMessageError("Unrecognized table type!");
        }
    }
}

// Recursive.
bool CreateDelta::apply(const std::unique_ptr<Connect> &e_conn,
                        TableType table_type)
{
    const std::string table_name = tableNameFromType(table_type);
    std::function<bool(const DBMeta &, const DBMeta &,
                       const AbstractMetaKey * const,
                       const unsigned int * const)> helper =
        [&e_conn, &helper, table_name] (const DBMeta &object,
                                        const DBMeta &parent,
                                        const AbstractMetaKey * const k,
                               const unsigned int * const ptr_parent_id)
    {
        const std::string child_serial = object.serialize(parent);
        assert(0 == object.getDatabaseID());
        unsigned int parent_id;
        if (ptr_parent_id) {
            parent_id = *ptr_parent_id;
        } else {
            parent_id = parent.getDatabaseID();
        }

        std::function<std::string(const DBMeta &, const DBMeta &,
                                  const AbstractMetaKey *const)>
            getSerialKey =
                [] (const DBMeta &p, const DBMeta &o,
                    const AbstractMetaKey *const keee)
            {
                if (NULL == keee) {
                    return p.getKey(o).getSerial();  /* lambda */
                }

                return keee->getSerial();      /* lambda */
            };

        const std::string serial_key = getSerialKey(parent, object, k);
        const std::string esc_serial_key =
            escapeString(e_conn, serial_key);

        // ------------------------
        //    Build the queries.
        // ------------------------

        // On CREATE, the database generates a unique ID for us.
        const std::string esc_child_serial =
            escapeString(e_conn, child_serial);

        const std::string query =
            " INSERT INTO " + table_name + 
            "    (serial_object, serial_key, parent_id) VALUES (" 
            " '" + esc_child_serial + "',"
            " '" + esc_serial_key + "',"
            " " + std::to_string(parent_id) + ");";
        RETURN_FALSE_IF_FALSE(e_conn->execute(query));

        const unsigned int object_id = e_conn->last_insert_id();

        std::function<bool(const DBMeta &)> localCreateHandler =
            [&object, object_id, &helper]
                (const DBMeta &child)
            {
                return helper(child, object, NULL, &object_id);
            };
        return object.applyToChildren(localCreateHandler);
    };

    return helper(*meta.get(), parent_meta, &key, NULL);
}

// FIXME: used incorrectly, as we should be doing copy construction
// on the original object; not modifying it in place
bool ReplaceDelta::apply(const std::unique_ptr<Connect> &e_conn,
                         TableType table_type)
{
    const std::string table_name = tableNameFromType(table_type);

    const unsigned int child_id = meta.getDatabaseID();

    const std::string child_serial = meta.serialize(parent_meta);
    const std::string esc_child_serial =
        escapeString(e_conn, child_serial);
    const std::string serial_key = key.getSerial();
    const std::string esc_serial_key = escapeString(e_conn, serial_key);

    const std::string query = 
        " UPDATE " + table_name +
        "    SET serial_object = '" + esc_child_serial + "', "
        "        serial_key = '" + esc_serial_key + "'"
        "  WHERE id = " + std::to_string(child_id) + ";";
    RETURN_FALSE_IF_FALSE(e_conn->execute(query));

    return true;
}

bool DeleteDelta::apply(const std::unique_ptr<Connect> &e_conn,
                        TableType table_type)
{
    const std::string table_name = tableNameFromType(table_type);
    Connect * const e_c = e_conn.get();
    std::function<bool(const DBMeta &, const DBMeta &)> helper =
        [&e_c, &helper, table_name](const DBMeta &object,
                                    const DBMeta &parent)
    {
        const unsigned int object_id = object.getDatabaseID();
        const unsigned int parent_id = parent.getDatabaseID();

        const std::string query =
            " DELETE " + table_name + " "
            "   FROM " + table_name +
            "  WHERE " + table_name + ".id" +
            "      = "     + std::to_string(object_id) +
            "    AND " + table_name + ".parent_id" +
            "      = "     + std::to_string(parent_id) + ";";
        RETURN_FALSE_IF_FALSE(e_c->execute(query));

        std::function<bool(const DBMeta &)> localDestroyHandler =
            [&object, &helper] (const DBMeta &child) {
                return helper(child, object);
            };
        return object.applyToChildren(localDestroyHandler);
    };

    return helper(meta, parent_meta);
}

RewriteOutput::~RewriteOutput()
{;}

bool RewriteOutput::stalesSchema() const
{
    return false;
}

bool RewriteOutput::multipleResultSets() const
{
    return false;
}

QueryAction
RewriteOutput::queryAction(const std::unique_ptr<Connect> &conn) const
{
    return QueryAction::DECRYPT;
}

bool
RewriteOutput::usesEmbeddedDB() const
{
    return false;
}

void
SimpleOutput::beforeQuery(const std::unique_ptr<Connect> &,
                          const std::unique_ptr<Connect> &)
{
    return;
}

void
SimpleOutput::getQuery(std::list<std::string> *const queryz,
                       SchemaInfo const &) const
{
    queryz->clear();
    queryz->push_back(original_query);

    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
SimpleOutput::afterQuery(const std::unique_ptr<Connect> &) const
{
    return std::make_pair(false, std::unique_ptr<DBResult>(nullptr));
}

QueryAction
SimpleOutput::queryAction(const std::unique_ptr<Connect> &conn) const
{
    return QueryAction::NO_DECRYPT;
}

void
DMLOutput::beforeQuery(const std::unique_ptr<Connect> &,
                       const std::unique_ptr<Connect> &)
{
    return;
}

void
DMLOutput::getQuery(std::list<std::string> * const queryz,
                    SchemaInfo const &) const
{
    queryz->clear();
    queryz->push_back(new_query);

    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
DMLOutput::afterQuery(const std::unique_ptr<Connect> &) const
{
    return std::make_pair(false, std::unique_ptr<DBResult>(nullptr));
}

// we can not successfully issue a special update while inside of a transaction
// as our attempt to retrieve rows from the database will fail to recover
// data inserted during the current transaction
// > this failure will occur in the stored procedure
void
SpecialUpdate::beforeQuery(const std::unique_ptr<Connect> &conn,
                           const std::unique_ptr<Connect> &e_conn)
{
    // Retrieve rows from database.
    const std::string select_q =
        " SELECT * FROM " + this->plain_table +
        " WHERE " + this->where_clause + ";";
    std::unique_ptr<SchemaCache> schema_cache(new SchemaCache());
    // Onion adjustment will never occur in this nested executeQuery(...)
    // because the WHERE clause will trigger the adjustment in 
    // UpdateHandler when it tries to rewrite the filters.
    const EpilogueResult epi_result =
        executeQuery(this->ps, select_q, this->default_db,
                     schema_cache.get());
    TEST_Sync(schema_cache->cleanupStaleness(e_conn),
              "failed to cleanup schema cache after nested query!");
    assert(QueryAction::DECRYPT == epi_result.action);
    const ResType select_res_type = epi_result.res_type;
    assert(select_res_type.success());
    if (select_res_type.rows.size() == 0) { // No work to be done.
        this->do_nothing = true;
        return;
    }
    this->do_nothing = false;

    const auto itemToNiceString =
        [&e_conn] (const std::shared_ptr<Item> &p_item)
        {
            const std::string &s = ItemToString(*p_item.get());

            if (Item::Type::STRING_ITEM != p_item->type()) {
                return s;
            }

            // escaping and quoting the string creates a value that can
            // actually be used in an INSERT statement
            return "'" + escapeString(e_conn, s) + "'";
        };

    // We must take these items and convert them into quoted, escaped
    // strings
    //  > Item -> std::string -> escaped -> quoted
    // then we join the results into a single comma seperated values list
    const auto pItemVectorToNiceValueList =
        [&itemToNiceString]
            (const std::vector<std::vector<std::shared_ptr<Item>>> &vec)
        {
            std::vector<std::string> esses;
            for (auto row_it : vec) {
                std::vector<std::string> nice_values(row_it.size());
                std::transform(row_it.begin(), row_it.end(),
                               nice_values.begin(), itemToNiceString);
                esses.push_back("("+ vector_join(nice_values, ",") + ")");
            }

            return vector_join(esses, ",");
        };

    const std::string &values_string =
        pItemVectorToNiceValueList(select_res_type.rows);
    // do the query on the embedded database inside of a transaction
    // so that we can prevent failure artifacts from populating the
    // embedded database
    TEST_Sync(e_conn->execute("START TRANSACTION;"),
              "failed to start transaction");

    // turn on strict mode so we can determine if we have bad values
    // > ie trying to insert 256 into a TINYINT UNSIGNED column
    SYNC_IF_FALSE(strictMode(e_conn.get()), e_conn);

    // Push the plaintext rows to the embedded database.
    const std::string push_q =
        " INSERT INTO " + this->plain_table +
        " VALUES " + values_string + ";";
    SYNC_IF_FALSE(e_conn->execute(push_q), e_conn);

    // Run the original (unmodified) query on the data in the embedded
    // database.
    SYNC_IF_FALSE(e_conn->execute(this->original_query), e_conn);

    // strict mode off
    SYNC_IF_FALSE(e_conn->execute("SET SESSION sql_mode = ''"), e_conn);

    // > Collect the results from the embedded database.
    // > This code relies on single threaded access to the database
    //   and on the fact that the database is cleaned up after
    //   every such operation.
    std::unique_ptr<DBResult> dbres;
    const std::string select_results_q =
        " SELECT * FROM " + this->plain_table + ";";
    SYNC_IF_FALSE(e_conn->execute(select_results_q, &dbres), e_conn);
    const ResType interim_res = ResType(dbres->unpack());
    this->escaped_output_values =
        pItemVectorToNiceValueList(interim_res.rows);

    // Cleanup the embedded database.
    const std::string cleanup_q =
        "DELETE FROM " + this->plain_table + ";";
    SYNC_IF_FALSE(e_conn->execute(cleanup_q), e_conn);

    SYNC_IF_FALSE(e_conn->execute("COMMIT;"), e_conn);

    return;
}

void
SpecialUpdate::getQuery(std::list<std::string> * const queryz,
                        SchemaInfo const &schema) const
{
    assert(queryz);

    queryz->clear();

    if (true == this->do_nothing.get()) {
        queryz->push_back(mysql_noop());
        return;
    }

    // This query is necessary to propagate a transaction into
    // INFORMATION_SCHEMA.
    queryz->push_back("SELECT NULL FROM " + this->crypted_table + ";");

    // DELETE the rows matching the WHERE clause from the database.
    const std::string delete_q =
        " DELETE FROM " + this->plain_table +
        " WHERE " + this->where_clause + ";";
    const std::string re_delete =
        rewriteAndGetSingleQuery(ps, delete_q, schema, this->default_db);

    // > Add each row from the embedded database to the data database.
    const std::string insert_q =
        " INSERT INTO " + this->plain_table +
        " VALUES " + this->escaped_output_values.get() + ";";
    const std::string re_insert =
        rewriteAndGetSingleQuery(ps, insert_q, schema, this->default_db);

    const std::string hom_addition_transaction =
        MetaData::Proc::homAdditionTransaction();
    queryz->push_back(" CALL " + hom_addition_transaction + " ("
                      " '" + escapeString(ps.getConn(), re_delete) + "', "
                      " '" + escapeString(ps.getConn(),
                                          re_insert) + "');");

    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
SpecialUpdate::afterQuery(const std::unique_ptr<Connect> &) const
{
    return std::make_pair(false, std::unique_ptr<DBResult>(nullptr));
}

bool
SpecialUpdate::multipleResultSets() const
{
    return true;
}

bool
SpecialUpdate::usesEmbeddedDB() const
{
    return true;
}

void
UseAfterQueryResultOutput::beforeQuery(const std::unique_ptr<Connect> &,
                                       const std::unique_ptr<Connect> &)
{
    return;
}

void
UseAfterQueryResultOutput::getQuery(std::list<std::string> * const queryz,
                                    SchemaInfo const &) const
{
    queryz->clear();
    queryz->push_back(mysql_noop());

    return;
}

static bool
deleteAllShowDirectiveEntries(const std::unique_ptr<Connect> &e_conn)
{
    const std::string &query =
        "DELETE FROM " + MetaData::Table::showDirective() + ";";
    return e_conn->execute(query);
}

static bool
addShowDirectiveEntry(const std::unique_ptr<Connect> &e_conn,
                      const std::string &database,
                      const std::string &table,
                      const std::string &field,
                      const std::string &onion,
                      const std::string &level)
{
    const std::string &query =
        "INSERT INTO " + MetaData::Table::showDirective() +
        " (_database, _table, _field, _onion, _level) VALUES "
        " ('" + database + "', '" + table + "',"
        "  '" + field + "', '" + onion + "', '" + level + "')";
    return e_conn->execute(query);
}

static bool
getAllShowDirectiveEntries(const std::unique_ptr<Connect> &e_conn,
                           std::unique_ptr<DBResult> *db_res)
{
    assert(db_res);
    const std::string &query =
        "SELECT * FROM " + MetaData::Table::showDirective() + ";";
    return e_conn->execute(query, db_res);
}

// HACK hack HACK hackity hackhack
std::pair<bool, std::unique_ptr<DBResult>>
UseAfterQueryResultOutput::afterQuery(const std::unique_ptr<Connect> &e_conn) const
{
    TEST_TextMessageError(deleteAllShowDirectiveEntries(e_conn),
                          "failed to initialize show directives table");

    const auto &databases = schema.children;
    for (auto db_it = databases.begin(); db_it != databases.end();
         ++db_it) {
        const std::string &db_name = db_it->first.getValue();
        const auto &dm = db_it->second;
        const auto &tables = dm->children;
        for (auto table_it = tables.begin(); table_it != tables.end();
             ++table_it) {
            const std::string &table_name = table_it->first.getValue();
            const auto &tm = table_it->second;
            const auto &fields = tm->children;
            for (auto field_it = fields.begin(); field_it != fields.end();
                 ++field_it) {
                const std::string &field_name =
                    field_it->first.getValue();
                const auto &fm = field_it->second;
                const auto &onions = fm->children;
                for (auto onion_it = onions.begin();
                     onion_it != onions.end();
                     ++onion_it) {
                    const std::string &onion_name =
                      TypeText<onion>::toText(onion_it->first.getValue());
                    const auto &om = onion_it->second;

                    // HACK: this behavior is not usually safe, use
                    // Analysis to get state information generally
                    const std::string &level =
                        TypeText<SECLEVEL>::toText(om->getSecLevel());
                    const bool b =
                        addShowDirectiveEntry(e_conn, db_name, table_name,
                                              field_name, onion_name,
                                              level);
                    TEST_TextMessageError(true == b,
                                          "failed producing directive"
                                          " results");
                }
            }
        }
    }

    std::unique_ptr<DBResult> db_res;
    TEST_TextMessageError(getAllShowDirectiveEntries(e_conn, &db_res),
                          "failed retrieving directive results");
    return std::make_pair(true, std::move(db_res));
}

QueryAction
UseAfterQueryResultOutput::queryAction(const std::unique_ptr<Connect> &)
    const
{
    return QueryAction::RETURN_AFTER;
}

DeltaOutput::~DeltaOutput()
{;}

bool DeltaOutput::stalesSchema() const
{
    return true;
}

void
DeltaOutput::beforeQuery(const std::unique_ptr<Connect> &conn,
                         const std::unique_ptr<Connect> &e_conn)
{
    TEST_Sync(e_conn->execute("START TRANSACTION;"),
              "failed to start transaction");

    // We must save the current default database because recovery
    // may be happening after a restart in which case such state
    // was lost.
    const CompletionType &completion_type = this->getCompletionType();
    const std::string &q_completion =
        " INSERT INTO " + MetaData::Table::embeddedQueryCompletion() +
        "   (begin, complete, original_query, default_db, aborted, type)"
        "   VALUES (TRUE,  FALSE,"
        "    '" + escapeString(conn, this->original_query) + "',"
        "    (SELECT DATABASE()),  FALSE,"
        "    '" + TypeText<CompletionType>::toText(completion_type)
            + "');";
    SYNC_IF_FALSE(e_conn->execute(q_completion), e_conn);
    this->embedded_completion_id = e_conn->last_insert_id();

    for (auto it = deltas.begin(); it != deltas.end(); it++) {
        const bool b = (*it)->apply(e_conn, Delta::BLEEDING_TABLE);
        SYNC_IF_FALSE(b, e_conn);
    }

    SYNC_IF_FALSE(e_conn->execute("COMMIT;"), e_conn);

    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
DeltaOutput::afterQuery(const std::unique_ptr<Connect> &e_conn) const
{
    TEST_Sync(e_conn->execute("START TRANSACTION;"),
              "failed to start transaction");

    const std::string q_update =
        " UPDATE " + MetaData::Table::embeddedQueryCompletion() +
        "    SET complete = TRUE"
        "  WHERE id=" +
                 std::to_string(this->embedded_completion_id.get()) + ";";
    SYNC_IF_FALSE(e_conn->execute(q_update), e_conn);

    for (auto it = deltas.begin(); it != deltas.end(); it++) {
        const bool b = (*it)->apply(e_conn, Delta::REGULAR_TABLE);
        SYNC_IF_FALSE(b, e_conn);
    }

    SYNC_IF_FALSE(e_conn->execute("COMMIT;"), e_conn);

    return std::make_pair(false, std::unique_ptr<DBResult>(nullptr));
}

bool
DeltaOutput::usesEmbeddedDB() const
{
    return true;
}

unsigned long
DeltaOutput::getEmbeddedCompletionID() const
{
    return this->embedded_completion_id.get();
}

static bool
tableCopy(const std::unique_ptr<Connect> &c, const std::string &src,
          const std::string &dest)
{
    const std::string delete_query =
        " DELETE FROM " + dest + ";";
    RETURN_FALSE_IF_FALSE(c->execute(delete_query));

    const std::string insert_query =
        " INSERT " + dest +
        "   SELECT * FROM " + src + ";";
    RETURN_FALSE_IF_FALSE(c->execute(insert_query));

    return true;
}

bool
setRegularTableToBleedingTable(const std::unique_ptr<Connect> &e_conn)
{
    const std::string src = MetaData::Table::bleedingMetaObject();
    const std::string dest = MetaData::Table::metaObject();
    return tableCopy(e_conn, src, dest);
}

bool
setBleedingTableToRegularTable(const std::unique_ptr<Connect> &e_conn)
{
    const std::string src = MetaData::Table::metaObject();
    const std::string dest = MetaData::Table::bleedingMetaObject();
    return tableCopy(e_conn, src, dest);
}

void
DDLOutput::getQuery(std::list<std::string> * const queryz,
                    SchemaInfo const &) const
{
    queryz->clear();

    assert(remote_qz().size() == 1);
    const std::string &remote_begin =
        " INSERT INTO " + MetaData::Table::remoteQueryCompletion() +
        "   (begin, complete, embedded_completion_id, reissue) VALUES"
        "   (TRUE,  FALSE," +
             std::to_string(this->getEmbeddedCompletionID()) +
        "    , FALSE);";
    const std::string &remote_complete =
        " UPDATE " + MetaData::Table::remoteQueryCompletion() +
        "    SET complete = TRUE"
        "  WHERE embedded_completion_id = " +
             std::to_string(this->getEmbeddedCompletionID()) + ";";

    queryz->push_back(remote_begin);
    queryz->push_back(remote_qz().back());
    queryz->push_back(remote_complete);

    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
DDLOutput::afterQuery(const std::unique_ptr<Connect> &e_conn) const
{
    // Update embedded database.
    // > This is a DDL query so do not put in transaction.
    TEST_Sync(e_conn->execute(this->original_query),
              "Failed to execute DDL query against embedded database!");

    return DeltaOutput::afterQuery(e_conn);
}

const std::list<std::string> DDLOutput::remote_qz() const
{
    return std::list<std::string>({new_query});
}

const std::list<std::string> DDLOutput::local_qz() const
{
    return std::list<std::string>({original_query});
}

CompletionType DDLOutput::getCompletionType() const
{
    return CompletionType::DDLCompletion;
}

void
AdjustOnionOutput::beforeQuery(const std::unique_ptr<Connect> &conn,
                               const std::unique_ptr<Connect> &e_conn)
{
    assert(deltas.size() > 0);
    return DeltaOutput::beforeQuery(conn, e_conn);
}

void
AdjustOnionOutput::getQuery(std::list<std::string> * const queryz,
                            SchemaInfo const &) const
{
    std::list<std::string> r_qz = remote_qz();
    assert(r_qz.size() == 1 || r_qz.size() == 2);

    if (r_qz.size() == 1) {
        r_qz.push_back(mysql_noop());
    }

    // This query is necessary to propagate a transaction into
    // INFORMATION_SCHEMA.
    // > This allows consistent behavior even when adjustment is first
    // query in transaction.
    const std::string &innodb_table =
        MetaData::Table::remoteQueryCompletion();
    queryz->push_back("SELECT NULL FROM " + innodb_table + ";");

    const std::string q_remote =
        " CALL " + MetaData::Proc::adjustOnion() + " ("
        "   "  + std::to_string(this->getEmbeddedCompletionID()) + ","
        "   '" + hackEscape(r_qz.front()) + "', "
        "   '" + hackEscape(r_qz.back()) + "');";

    queryz->push_back(q_remote);
    return;
}

std::pair<bool, std::unique_ptr<DBResult>>
AdjustOnionOutput::afterQuery(const std::unique_ptr<Connect> &e_conn)
    const
{
    assert(deltas.size() > 0);
    return DeltaOutput::afterQuery(e_conn);
}

const std::list<std::string> AdjustOnionOutput::remote_qz() const
{
    return std::list<std::string>(adjust_queries);
}

const std::list<std::string> AdjustOnionOutput::local_qz() const
{
    return std::list<std::string>();
}

QueryAction
AdjustOnionOutput::queryAction(const std::unique_ptr<Connect> &conn)
    const
{
    const std::string q =
        " SELECT reissue "
        "   FROM " + MetaData::Table::remoteQueryCompletion() +
        "  WHERE embedded_completion_id = " +
                 std::to_string(this->getEmbeddedCompletionID()) + ";";

    std::unique_ptr<DBResult> db_res;
    TEST_Text(conn->execute(q, &db_res),
              "failed to determine if an onion adjustmented query should"
              " be reissued!");
    assert(1 == mysql_num_rows(db_res->n));

    const MYSQL_ROW row = mysql_fetch_row(db_res->n);
    const unsigned long *const l = mysql_fetch_lengths(db_res->n);
    assert(l != NULL);

    const bool reissue = string_to_bool(std::string(row[0], l[0]));

    return reissue ? QueryAction::AGAIN : QueryAction::ROLLBACK;
}

CompletionType AdjustOnionOutput::getCompletionType() const
{
    return CompletionType::AdjustOnionCompletion;
}

bool Analysis::addAlias(const std::string &alias,
                        const std::string &db,
                        const std::string &table)
{
    auto db_alias_pair = table_aliases.find(db);
    if (table_aliases.end() == db_alias_pair) {
        table_aliases.insert(
           make_pair(db,
                     std::map<const std::string, const std::string>()));
    }

    std::map<const std::string, const std::string> &
        per_db_table_aliases = table_aliases[db];
    auto alias_pair = per_db_table_aliases.find(alias);
    if (per_db_table_aliases.end() != alias_pair) {
        return false;
    }

    per_db_table_aliases.insert(make_pair(alias, table));
    return true;
}

OnionMeta &Analysis::getOnionMeta(const std::string &db,
                                  const std::string &table,
                                  const std::string &field,
                                  onion o) const
{
    return this->getOnionMeta(this->getFieldMeta(db, table, field), o);
}

OnionMeta &Analysis::getOnionMeta(const FieldMeta &fm,
                                  onion o) const
{
    OnionMeta *const om = fm.getOnionMeta(o);
    TEST_IdentifierNotFound(om, TypeText<onion>::toText(o));

    return *om;
}

FieldMeta &Analysis::getFieldMeta(const std::string &db,
                                  const std::string &table,
                                  const std::string &field) const
{
    FieldMeta * const fm =
        this->getTableMeta(db, table).getChild(IdentityMetaKey(field));
    TEST_IdentifierNotFound(fm, field);

    return *fm;
}

FieldMeta &Analysis::getFieldMeta(const TableMeta &tm,
                                  const std::string &field) const
{
    FieldMeta *const fm = tm.getChild(IdentityMetaKey(field));
    TEST_IdentifierNotFound(fm, field);

    return *fm;
}

TableMeta &Analysis::getTableMeta(const std::string &db,
                                  const std::string &table) const
{
    const DatabaseMeta &dm = this->getDatabaseMeta(db);

    TableMeta *const tm =
        dm.getChild(IdentityMetaKey(unAliasTable(db, table)));
    TEST_IdentifierNotFound(tm, table);

    return *tm;
}

DatabaseMeta &
Analysis::getDatabaseMeta(const std::string &db) const
{
    DatabaseMeta *const dm = this->schema.getChild(IdentityMetaKey(db));
    TEST_DatabaseNotFound(dm, db);

    return *dm;
}

bool Analysis::tableMetaExists(const std::string &db,
                               const std::string &table) const
{
    return this->nonAliasTableMetaExists(db, unAliasTable(db, table));
}

bool Analysis::nonAliasTableMetaExists(const std::string &db,
                                       const std::string &table) const
{
    const DatabaseMeta &dm = this->getDatabaseMeta(db);
    return dm.childExists(IdentityMetaKey(table));
}

bool
Analysis::databaseMetaExists(const std::string &db) const
{
    return this->schema.childExists(IdentityMetaKey(db));
}

std::string Analysis::getAnonTableName(const std::string &db,
                                       const std::string &table,
                                       bool *const is_alias) const
{
    // tell the caller if you are giving him an alias
    if (is_alias) {
        *is_alias = this->isAlias(db, table);
    }

    if (this->isAlias(db, table)) {
        return table;
    }

    return this->getTableMeta(db, table).getAnonTableName();
}

std::string
Analysis::translateNonAliasPlainToAnonTableName(const std::string &db,
                                                const std::string &table)
    const
{
    TableMeta *const tm =
        this->getDatabaseMeta(db).getChild(IdentityMetaKey(table));
    TEST_IdentifierNotFound(tm, table);

    return tm->getAnonTableName();
}

std::string Analysis::getAnonIndexName(const std::string &db,
                                       const std::string &table,
                                       const std::string &index_name,
                                       onion o)
    const
{
    return this->getTableMeta(db, table).getAnonIndexName(index_name, o);
}

std::string Analysis::getAnonIndexName(const TableMeta &tm,
                                       const std::string &index_name,
                                       onion o)
    const
{
    return tm.getAnonIndexName(index_name, o);
}

bool Analysis::isAlias(const std::string &db,
                       const std::string &table) const
{
    auto db_alias_pair = table_aliases.find(db);
    if (table_aliases.end() == db_alias_pair) {
        return false;
    }

    return db_alias_pair->second.end() != db_alias_pair->second.find(table);
}

std::string Analysis::unAliasTable(const std::string &db,
                                   const std::string &table) const
{
    auto db_alias_pair = table_aliases.find(db);
    if (table_aliases.end() == db_alias_pair) {
        return table;
    }

    auto alias_pair = db_alias_pair->second.find(table);
    if (db_alias_pair->second.end() == alias_pair) {
        return table;
    }
    
    // We've found an alias!
    return alias_pair->second;
}

EncLayer &Analysis::getBackEncLayer(const OnionMeta &om)
{
    return *om.layers.back().get();
}

SECLEVEL Analysis::getOnionLevel(const OnionMeta &om)
{
    return om.getSecLevel();
}

SECLEVEL Analysis::getOnionLevel(const FieldMeta &fm, onion o)
{
    if (false == fm.hasOnion(o)) {
        return SECLEVEL::INVALID;
    }

    return Analysis::getOnionLevel(this->getOnionMeta(fm, o));
}

const std::vector<std::unique_ptr<EncLayer> > &
Analysis::getEncLayers(const OnionMeta &om)
{
    return om.layers;
}

RewritePlanWithAnalysis::RewritePlanWithAnalysis(const EncSet &es_out,
                                                 reason r,
                                            std::unique_ptr<Analysis> a)
    : RewritePlan(es_out, r), a(std::move(a))
{}

