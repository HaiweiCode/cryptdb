#include <main/Analysis.hh>

using namespace std;


EncSet::EncSet() : osl(FULL_EncSet.osl) {}

EncSet::EncSet(FieldMeta * fm) {
    osl.clear();
    for (auto pair : fm->onions) {
	osl[pair.first] = LevelFieldPair(pair.second->getSecLevel(), fm);
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

        FieldMeta *fm = it->second.second;
        FieldMeta *fm2 = it2->second.second;

        if (it != osl.end()) {
            SECLEVEL sl = (SECLEVEL)min((int)it->second.first,
                    (int)it2->second.first);
	    onion o = it->first;

            if (fm == NULL) {
                m[o] = LevelFieldPair(sl, fm2);
            } else if (fm2 == NULL) {
                m[it->first] = LevelFieldPair(sl, fm);
            } else if (fm != NULL && fm2 != NULL) {
                // TODO(burrows): Guarentee that the keys are the same.
		if (sl == SECLEVEL::DETJOIN) {
		    m[o] = LevelFieldPair(sl, fm);
		} else if (sl == SECLEVEL::HOM) {
                    m[o] = LevelFieldPair(sl, fm);
                }
            }
        }
    }
    return EncSet(m);
}

ostream&
operator<<(ostream &out, const EncSet & es)
{
    if (es.osl.size() == 0) {
        out << "empty encset";
    }
    for (auto it : es.osl) {
        out << "(onion " << it.first
            << ", level " << levelnames[(int)it.second.first]
            << ", field `" << (it.second.second == NULL ? "*" : it.second.second->fname) << "`"
            << ") ";
    }
    return out;
}


void
EncSet::setFieldForOnion(onion o, FieldMeta * fm) {
    LevelFieldPair lfp = getAssert(osl, o);

    osl[o] = LevelFieldPair(lfp.first, fm);
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

    
    static size_t onion_size = sizeof(onion_order) / sizeof(onion_order[0]);
    for (size_t i = 0; i < onion_size; i++) {
	onion o = onion_order[i];
        auto it = osl.find(o);
        if (it != osl.end()) {
            if (it->second.second == 0 && it->second.first != SECLEVEL::PLAINVAL) {
                /*
                 * If no key, skip this OLK.
                 * What are the semantics of chooseOne() anyway?
                 */
                continue;
            }

	    return OLK(o,  it->second.first, it->second.second);
        }
    }
    return OLK();
}

bool
EncSet::contains(const OLK & olk) const {
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
needsSalt(EncSet es) {
    for (auto pair : es.osl) {
	if (pair.second.first == SECLEVEL::RND) {
	    return true;
	}
    }

    return false;
}


ostream&
operator<<(ostream &out, const reason &r)
{
    out << r.why_t_item << " PRODUCES encset " << r.encset << "\n" \
	<< " BECAUSE " << r.why_t << "\n";
    
    if (r.childr->size()) {
	out << " AND CHILDREN: {" << "\n";
	for (reason ch : *r.childr) {
	    out << ch;
	}
	out << "} \n";
    }
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

ostream&
operator<<(ostream &out, const RewritePlan * rp)
{
    if (!rp) {
        out << "NULL RewritePlan";
	return out;
    }

    out << " RewritePlan: \n---> out encset " << rp->es_out << "\n---> reason " << rp->r << "\n";
    
    return out;
}

bool Analysis::addAlias(std::string alias, std::string table)
{
    auto alias_pair = table_aliases.find(alias);
    if (table_aliases.end() != alias_pair) {
        return false;
    }

    table_aliases[alias] = table;
    return true;
}

FieldMeta *Analysis::getFieldMeta(std::string table, std::string field) const
{
    string real_table_name = unAliasTable(table);
    return ps->schema->getFieldMeta(real_table_name, field);
}

TableMeta *Analysis::getTableMeta(std::string table) const
{
    string real_table_name = unAliasTable(table);
    return ps->schema->getTableMeta(real_table_name);
}

std::string Analysis::unAliasTable(std::string table) const
{
    auto alias_pair = table_aliases.find(table);
    if (table_aliases.end() != alias_pair) {
        return alias_pair->second;
    } else {
        return table;
    }
}

