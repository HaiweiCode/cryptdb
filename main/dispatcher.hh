#pragma once

#include <map>

#include <main/Analysis.hh>
#include <main/sql_handler.hh>
#include <main/alter_sub_handler.hh>

#include <sql_lex.h>

template <typename Input, typename FetchMe>
class Dispatcher {
public:
    virtual ~Dispatcher() {}

    bool addHandler(long long cmd, FetchMe *h) {
        if (NULL == h) {
            return false;
        }

        auto it = handlers.find(cmd);
        if (handlers.end() != it) {
            return false;
        }

        handlers[cmd] = std::unique_ptr<FetchMe>(h);
        return true;
    }

    bool canDo(Input lex) const {
        return handlers.end() != handlers.find(extract(lex));
    }

    const FetchMe &dispatch(Input lex) const {
        auto it = handlers.find(extract(lex));
        assert(handlers.end() != it);

        assert(it->second);
        return *it->second;
    }

    std::map<long long, std::unique_ptr<FetchMe>> handlers;

private:
    virtual long long extract(Input lex) const = 0;
};

class SQLDispatcher : public Dispatcher<LEX*, SQLHandler> {
    virtual long long extract(LEX* lex) const;
};

class AlterDispatcher : public Dispatcher<LEX*, AlterSubHandler> {
    virtual long long extract(LEX* lex) const;
    long calculateMask() const;
};

