#include <main/ddl_handler.hh>
#include <main/rewrite_util.hh>
#include <main/rewrite_main.hh>
#include <main/alter_sub_handler.hh>
#include <main/dispatcher.hh>
#include <main/macro_util.hh>
#include <parser/lex_util.hh>

class CreateTableHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        TEST_DatabaseDiscrepancy(pre.dbname, a.getDatabaseName());
        LEX *const new_lex = copyWithTHD(lex);

        //TODO: support for "create table like"
        TEST_TextMessageError(
                !(lex->create_info.options & HA_LEX_CREATE_TABLE_LIKE),
                "No support for create table like yet. "
                "If you see this, please implement me");

        // Create the table regardless of 'IF NOT EXISTS' if the table
        // doesn't exist.
        if (false == a.tableMetaExists(pre.dbname, pre.table)) {
            // TODO: Use appropriate values for has_sensitive and has_salt.
            std::unique_ptr<TableMeta> tm(new TableMeta(true, true));

            // -----------------------------
            //         Rewrite TABLE       
            // -----------------------------
            // HACK.
            // > We know that there is only one table.
            // > We do not currently support CREATE + SELECT syntax
            //   ! CREATE TABLE t2 SELECT * FROM t1;
            // > We also know that Analysis does not have a reference to
            //   the table as it depends on SchemaInfo.
            // > And we know that the table we want is tm with name table.
            // > This will _NOT_ gracefully handle a malformed CREATE TABLE
            // query.
            TEST_Text(1 == new_lex->select_lex.table_list.elements,
                      "we do not support multiple tables in a CREATE"
                      " TABLE queries");
            // Take the table name straight from 'tm' as
            // Analysis::getAnonTableName relies on SchemaInfo.
            TABLE_LIST *const tbl =
                rewrite_table_list(new_lex->select_lex.table_list.first,
                                   tm->getAnonTableName());
            new_lex->select_lex.table_list =
                *oneElemListWithTHD<TABLE_LIST>(tbl);

            auto it =
                List_iterator<Create_field>(lex->alter_info.create_list);
            new_lex->alter_info.create_list =
                accumList<Create_field>(it,
                    [&a, &ps, &tm] (List<Create_field> out_list,
                                    Create_field *const cf) {
                        return createAndRewriteField(a, ps, cf, tm.get(),
                                                     true, out_list);
                });

            // -----------------------------
            //         Rewrite INDEX
            // -----------------------------
            highLevelRewriteKey(*tm.get(), *lex, new_lex, a);

            // -----------------------------
            //         Update TABLE
            // -----------------------------
            a.deltas.push_back(std::unique_ptr<Delta>(
                            new CreateDelta(std::move(tm),
                                            a.getDatabaseMeta(pre.dbname),
                                            IdentityMetaKey(pre.table))));
        } else { // Table already exists.

            // Make sure we aren't trying to create a table that
            // already exists.
            const bool test =
                lex->create_info.options & HA_LEX_CREATE_IF_NOT_EXISTS;
            TEST_TextMessageError(test,
                                "Table " + pre.table + " already exists!");

            // -----------------------------
            //         Rewrite TABLE
            // -----------------------------
            new_lex->select_lex.table_list =
                rewrite_table_list(lex->select_lex.table_list, a);
            // > We do not rewrite the fields because presumably the caller
            // can do a CREATE TABLE IF NOT EXISTS for a table that already
            // exists, but with fields that do not actually exist.
            // > This would cause problems when trying to look up FieldMeta
            // for these non-existant fields.
            // > We may want to do some additional non-deterministic
            // anonymization of the fieldnames to prevent information leaks.
            // (ie, server gets compromised, server logged all sql queries,
            // attacker can see that the admin creates the account table
            // with the credit card field every time the server boots)
        }

        return new_lex;
    }
};

// mysql does not support indiscriminate add-drops
// ie, 
//      mysql> create table pk (x integer);
//      Query OK, 0 rows affected (0.09 sec)
//
//      mysql> alter table pk drop column x, add column x integer,
//                            drop column x;
//      ERROR 1091 (42000): Can't DROP 'x'; check that column/key exists
//
//      mysql> alter table pk drop column x, add column x integer;
//      Query OK, 0 rows affected (0.03 sec)
//      Records: 0  Duplicates: 0  Warnings: 0
class AlterTableHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        TEST_Text(sub_dispatcher->canDo(lex),
                  "your ALTER TABLE query may require at least one"
                  " unsupported feature");
        const std::vector<AlterSubHandler *> &handlers =
            sub_dispatcher->dispatch(lex);
        assert(handlers.size() > 0);

        LEX *new_lex = copyWithTHD(lex);

        for (auto it : handlers) {
            new_lex = it->transformLex(a, new_lex, ps);
        }

        // -----------------------------
        //         Rewrite TABLE
        // -----------------------------
        // > Rewrite after doing the transformations as the handlers
        // expect the original table name to be intact.
        new_lex->select_lex.table_list =
            rewrite_table_list(new_lex->select_lex.table_list, a, true);

        return new_lex;
    }

    const std::unique_ptr<AlterDispatcher> sub_dispatcher;

public:
    AlterTableHandler() : sub_dispatcher(buildAlterSubDispatcher()) {}
};

class DropTableHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        LEX *const final_lex = rewrite(a, lex, ps);
        update(a, lex, ps);

        return final_lex;
    }
    
    LEX *rewrite(Analysis &a, LEX *lex, const ProxyState &ps) const
    {
        LEX *const new_lex = copyWithTHD(lex);
        new_lex->select_lex.table_list =
            rewrite_table_list(lex->select_lex.table_list, a, true);

        return new_lex;
    }

    void update(Analysis &a, LEX *lex, const ProxyState &ps) const
    {
        TABLE_LIST *tbl = lex->select_lex.table_list.first;
        for (; tbl; tbl = tbl->next_local) {
            char* table  = tbl->table_name;

            TEST_DatabaseDiscrepancy(tbl->db, a.getDatabaseName());
            if (lex->drop_if_exists) {
                if (false == a.tableMetaExists(tbl->db, table)) {
                    continue;
                }
            }

            // Remove from *Meta structures.
            TableMeta const &tm = a.getTableMeta(tbl->db, table);
            a.deltas.push_back(std::unique_ptr<Delta>(
                            new DeleteDelta(tm,
                                            a.getDatabaseMeta(tbl->db))));
        }
    }
};

class CreateDBHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *const lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        const std::string &dbname =
            convert_lex_str(lex->name);
        if (false == a.databaseMetaExists(dbname)) {
            std::unique_ptr<DatabaseMeta> dm(new DatabaseMeta());
            a.deltas.push_back(std::unique_ptr<Delta>(
                        new CreateDelta(std::move(dm), a.getSchema(),
                                        IdentityMetaKey(dbname))));
        } else {
            const bool test =
                lex->create_info.options & HA_LEX_CREATE_IF_NOT_EXISTS;
            TEST_TextMessageError(test,
                                "Database " + dbname + " already exists!");
        }

        return copyWithTHD(lex);
    }
};

class ChangeDBHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *const lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        a.special_query = Analysis::SpecialQuery::NO_CHANGE_META_DDL;
        return copyWithTHD(lex);
    }
};

class DropDBHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *const lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        const std::string &dbname =
            convert_lex_str(lex->name);
        const DatabaseMeta &dm = a.getDatabaseMeta(dbname);
        a.deltas.push_back(std::unique_ptr<Delta>(
                                    new DeleteDelta(dm, a.getSchema())));

        return copyWithTHD(lex);
    }
};

class LockTablesHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *const lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        LEX *const new_lex = copyWithTHD(lex);
        new_lex->select_lex.table_list =
            rewrite_table_list(lex->select_lex.table_list, a);

        return new_lex;
    }
};

class CreateIndexHandler : public DDLHandler {
    virtual LEX *rewriteAndUpdate(Analysis &a, LEX *const lex,
                                  const ProxyState &ps,
                                  const Preamble &pre) const
    {
        LEX *const new_lex = copyWithTHD(lex);

        // rewrite table
        new_lex->select_lex.table_list =
            rewrite_table_list(lex->select_lex.table_list, a);

        TEST_DatabaseDiscrepancy(pre.dbname, a.getDatabaseName());
        TableMeta const &tm = a.getTableMeta(pre.dbname, pre.table);

        highLevelRewriteKey(tm, *lex, new_lex, a);

        return new_lex;
    }
};

static std::string
empty_if_null(const char *const p)
{
    if (p) return std::string(p);

    return std::string("");
}

LEX *DDLHandler::transformLex(Analysis &a, LEX *lex,
                              const ProxyState &ps) const
{
    AssignOnce<std::string> db;
    AssignOnce<std::string> table;
    if (lex->select_lex.table_list.first) {
        db = empty_if_null(lex->select_lex.table_list.first->db);
        table =
            empty_if_null(lex->select_lex.table_list.first->table_name);
    } else {
        db =  "", table = "";
    }

    return this->rewriteAndUpdate(a, lex, ps, Preamble(db.get(),
                                                       table.get()));
}

// FIXME: Add test to make sure handler added successfully.
SQLDispatcher *buildDDLDispatcher()
{
    DDLHandler *h;
    SQLDispatcher *dispatcher = new SQLDispatcher();

    h = new CreateTableHandler();
    dispatcher->addHandler(SQLCOM_CREATE_TABLE, h);

    h = new AlterTableHandler();
    dispatcher->addHandler(SQLCOM_ALTER_TABLE, h);

    h = new DropTableHandler();
    dispatcher->addHandler(SQLCOM_DROP_TABLE, h);

    h = new CreateDBHandler();
    dispatcher->addHandler(SQLCOM_CREATE_DB, h);

    h = new ChangeDBHandler();
    dispatcher->addHandler(SQLCOM_CHANGE_DB, h);

    h = new DropDBHandler();
    dispatcher->addHandler(SQLCOM_DROP_DB, h);

    h = new LockTablesHandler();
    dispatcher->addHandler(SQLCOM_LOCK_TABLES, h);

    h = new CreateIndexHandler();
    dispatcher->addHandler(SQLCOM_CREATE_INDEX, h);

    return dispatcher;
}
