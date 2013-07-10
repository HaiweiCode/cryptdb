#pragma once

/*
 * TestQueries.h
 *
 */

#include <signal.h>
#include <stdlib.h>

#include <main/rewrite_main.hh>

#include <test/test_utils.hh>

typedef enum test_mode {
    UNENCRYPTED, SINGLE, MULTI,
    PROXYPLAIN, PROXYSINGLE, PROXYMULTI,
    TESTINVALID
} test_mode;

struct QueryChoice {
    const std::vector<std::string> plain;
    const std::vector<std::string> single;
    const std::vector<std::string> multi;

    QueryChoice(const std::vector<std::string> &plain_arg,
                const std::vector<std::string> &single_arg,
                const std::vector<std::string> &multi_arg)
        : plain(plain_arg), single(single_arg), multi(multi_arg)
    {
        assert(plain_arg.size() == single_arg.size());
        assert(plain_arg.size() == multi_arg.size());
    }

    const std::vector<std::string> &choose(test_mode t) const {
        switch (t) {
        case UNENCRYPTED:
        case PROXYPLAIN:
            return plain;

        case SINGLE:
        case PROXYSINGLE:
            return single;

        case MULTI:
        case PROXYMULTI:
            return multi;

        default:
            assert(0);
        }
    }

    size_t size() const {
        return plain.size();
    }
};

struct QueryList {
    std::string name;
    QueryChoice create;
    std::vector<Query> common;
    QueryChoice drop;

    QueryList(std::string namearg,
              std::vector<std::string> pc, std::vector<std::string> sc, std::vector<std::string> mc,
              std::vector<Query> c,
              std::vector<std::string> pd, std::vector<std::string> sd, std::vector<std::string> md)
        : name(namearg),
          create(pc, sc, mc),
          common(c),
          drop(pd, sd, md)
    {}
};

class Connection {
 public:
    Connection(const TestConfig &tc, test_mode type);
    ~Connection();

    ResType execute(std::string query);
    my_ulonglong executeLast();

    void restart();
    void start();
    void stop();

 private:
    test_mode type;
    TestConfig tc;

    //TODO/FIXME: check this
    Rewriter * re;
    
    //Rewriter object for proxy
    Rewriter * re_proxy;

    //It allow multiple proxy connections however CryptDB 
    //doesn't seem to support multiple proxy connections, anyways
    //it serves as a temporary solution for Rewriter instance issue.
    std::set<Rewriter *> re_set;

    //Connect objects for plain 
    std::set<Connect *> conn_set;

    //current connection we are on, for multiple connections
    std::set<Connect *>::iterator conn;
    
    //current connection we are on
    std::set<Rewriter *>::iterator re_it;
    
    pid_t proxy_pid;

    ResType executeConn(std::string query);
    ResType executeEDBProxy(std::string query);
    ResType executeRewriter(std::string query);

    my_ulonglong executeLastConn();
    my_ulonglong executeLastEDB();
    my_ulonglong executeRewriter();

    void executeFail(std::string query);
};

class TestQueries {
 public:
    TestQueries();
    ~TestQueries();

    static void run(const TestConfig &tc, int argc, char ** argv);

};
