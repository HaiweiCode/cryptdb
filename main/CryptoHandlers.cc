#include <main/CryptoHandlers.hh>
#include <main/macro_util.hh>
#include <main/schema.hh>
#include <parser/lex_util.hh>
#include <parser/mysql_type_metadata.hh>
#include <crypto/ope.hh>
#include <crypto/BasicCrypto.hh>
#include <crypto/SWPSearch.hh>
#include <crypto/arc4.hh>
#include <util/util.hh>
#include <util/cryptdb_log.hh>
#include <util/zz.hh>

#include <cmath>
#include <memory>

#define LEXSTRING(cstr) { (char*) cstr, sizeof(cstr) }

using namespace NTL;

#include <utility>

/*
 * Don't try to manually pull values out of Items with Item_int::value
 * because sometimes your 'Item_int', is actually an Item_string and you
 * need to either let MySQL code handle the conversion (Item::val_uint())
 * or you need to handle it yourself (strtoull).
 */

/* Implementation class hierarchy is as in .hh file plus:

   - We have a set of implementation EncLayer-s: each implementation layer
   is tied to some specific encryption scheme and key/block size:
   RND_int, RND_str, DET_int, DET_str, OPE_int, OPE_str

   - LayerFactory: creates an implementation EncLayer for a certain security
   level and field type:

   - RNDFactory: outputs a RND layer
         - RND layers: RND_int for blowfish, RND_str for AES

    -DETFactory: outputs a DET layer
         - DET layers: DET_int, DET_str

    -OPEFactory: outputs a OPE layer
         - OPE layers: OPE_int, OPE_str, OPE_dec

    -HOMFactory: outputs a HOM layer
         - HOM layers: HOM (for integers), HOM_dec (for decimals)
  
 */


//============= FACTORIES ==========================//

struct SerialLayer;

class LayerFactory {
public:
    static std::unique_ptr<EncLayer>
    create(Create_field * const cf, const std::string &key) {
        throw "needs to be inherited";
    };
    static std::unique_ptr<EncLayer>
    deserialize(const SerialLayer &serial) {
        throw "needs to be inherited";
    };
};

class RNDFactory : public LayerFactory {
public:
    static std::unique_ptr<EncLayer>
        create(Create_field * const cf, const std::string &key);
    static std::unique_ptr<EncLayer>
        deserialize(unsigned int id, const SerialLayer &serial);
};


class DETFactory : public LayerFactory {
public:
    static std::unique_ptr<EncLayer>
        create(Create_field * const cf, const std::string &key);
    static std::unique_ptr<EncLayer>
        deserialize(unsigned int id, const SerialLayer &serial);
};


class DETJOINFactory : public LayerFactory {
public:
    static std::unique_ptr<EncLayer>
        create(Create_field * const cf, const std::string &key);
    static std::unique_ptr<EncLayer>
        deserialize(unsigned int id, const SerialLayer &serial);
};

class OPEFactory : public LayerFactory {
public:
    static std::unique_ptr<EncLayer>
        create(Create_field * const cf, const std::string &key);
    static std::unique_ptr<EncLayer>
        deserialize(unsigned int id, const SerialLayer &serial);
};


class HOMFactory : public LayerFactory {
public:
    static std::unique_ptr<EncLayer>
        create(Create_field * const cf, const std::string &key);
    static std::unique_ptr<EncLayer>
        deserialize(unsigned int id, const SerialLayer &serial);
};


/*=====================  SERIALIZE Helpers =============================*/

struct SerialLayer {
    SECLEVEL l;
    std::string name;
    std::string layer_info;
};

static SerialLayer
serial_unpack(std::string serial)
{
    SerialLayer sl;
    
    std::stringstream ss(serial);
    uint len;
    ss >> len;
    std::string levelname;
    ss >> levelname;
    sl.l = TypeText<SECLEVEL>::toType(levelname);
    ss >> sl.name;
    sl.layer_info = serial.substr(serial.size()-len, len);

    return sl;
}

/*
static uint
getDecimals(const std::string &serial)
{
    return atoi(serial.substr(0, serial.find(' ')).c_str());
}

static std::string
underSerial(const std::string &serial)
{
    const uint pos = serial.find(" ");
    return serial.substr(pos + 1, std::string::npos);
}
*/

// ============================ Factory implementations ====================//


std::unique_ptr<EncLayer>
EncLayerFactory::encLayer(onion o, SECLEVEL sl, Create_field * const cf,
                          const std::string &key)
{
    switch (sl) {
        case SECLEVEL::RND: {return RNDFactory::create(cf, key);}
        case SECLEVEL::DET: {return DETFactory::create(cf, key);}
        case SECLEVEL::DETJOIN: {return DETJOINFactory::create(cf, key);}
        case SECLEVEL::OPE:{return OPEFactory::create(cf, key);}
        case SECLEVEL::HOM: {return HOMFactory::create(cf, key);}
        case SECLEVEL::SEARCH: {
            return std::unique_ptr<EncLayer>(new Search(cf, key));
        }
        case SECLEVEL::PLAINVAL: {
            return std::unique_ptr<EncLayer>(new PlainText());
        }

        default:{}
    }
    FAIL_TextMessageError("unknown or unimplemented security level");
}

std::unique_ptr<EncLayer>
EncLayerFactory::deserializeLayer(unsigned int id,
                                  const std::string &serial)
{
    assert(id);
    const SerialLayer li = serial_unpack(serial);

    switch (li.l) {
        case SECLEVEL::RND: 
            return RNDFactory::deserialize(id, li);

        case SECLEVEL::DET: 
            return DETFactory::deserialize(id, li);

        case SECLEVEL::DETJOIN: 
            return DETJOINFactory::deserialize(id, li);

        case SECLEVEL::OPE: 
            return OPEFactory::deserialize(id, li);

        case SECLEVEL::HOM:
            return std::unique_ptr<EncLayer>(new HOM(id, serial));

        case SECLEVEL::SEARCH:
            return std::unique_ptr<EncLayer>(new Search(id, serial));

        case SECLEVEL::PLAINVAL:
            return std::unique_ptr<EncLayer>(new PlainText(id));

        default:{}
    }
    FAIL_TextMessageError("unknown or unimplemented security level");
}

/*
string
EncLayerFactory::serializeLayer(EncLayer * el, DBMeta *parent) {
    return serial_pack(el->level(), el->name(), el->serialize(*parent));
}
*/

/* ========================= other helpers ============================*/

/*
static bool
stringItem(Item * const i)
{
    return i->type() == Item::Type::STRING_ITEM;
}
*/

static
std::string prng_expand(const std::string &seed_key, uint key_bytes)
{
    streamrng<arc4> prng(seed_key);
    return prng.rand_string(key_bytes);
}

//TODO: remove above newcreatefield
static Create_field*
createFieldHelper(const Create_field * const f,
                  unsigned long field_length,
                  enum enum_field_types type,
                  const std::string &anonname = "",
                  CHARSET_INFO * const charset = NULL)
{
    const THD * const thd = current_thd;
    Create_field * const f0 = f->clone(thd->mem_root);
    f0->length = field_length;
    f0->sql_type = type;

    if (charset != NULL) {
        f0->charset = charset;
    } else {
        //encryption is always unsigned
        f0->flags = f0->flags | UNSIGNED_FLAG;
    }

    if (anonname.size() > 0) {
        f0->field_name = make_thd_string(anonname);
    }

    return f0;

}

static Item *
get_key_item(const std::string &key)
{
    Item_string * const keyI =
        new Item_string(make_thd_string(key), key.length(),
                        &my_charset_bin);
    keyI->name = NULL; // no alias
    return keyI;
}



class CryptedInteger {
public:
    CryptedInteger(const Create_field &cf, const std::string &key)
        : key(key), field_type(cf.sql_type) {}
    static CryptedInteger
        deserialize(const std::string &serial);
    CryptedInteger(const std::string &key, enum enum_field_types type,
                   int64_t inclusiveMinimum, uint64_t inclusiveMaximum)
        : key(key), field_type(type), inclusiveMinimum(inclusiveMinimum),
          inclusiveMaximum(inclusiveMaximum) {}
    std::string serialize() const;
    bool validInput(const Item &i) const;
    std::string getKey() const {return key;}

private:
    const std::string key;
    enum enum_field_types field_type;
    int64_t inclusiveMinimum;
    uint64_t inclusiveMaximum;
};

CryptedInteger
CryptedInteger::deserialize(const std::string &serial)
{
    const std::vector<std::string> &vec = unserialize_string(serial);
    const std::string &key = vec[0];
    const enum enum_field_types field_type =
        TypeText<enum enum_field_types>::toType(vec[1]);
    const int64_t inclusiveMinimum = strtoll(vec[2].c_str(), NULL, 0);
    const int64_t inclusiveMaximum = strtoull(vec[3].c_str(), NULL, 0);

    return CryptedInteger(key, field_type, inclusiveMinimum,
                          inclusiveMaximum);
}

static std::string
serializeStrings(std::initializer_list<std::string> inputs)
{
    std::string out;
    for (auto it : inputs) {
        out += serialize_string(it);
    }

    return out;
}

std::string
CryptedInteger::serialize() const
{
    return serializeStrings({key,
            TypeText<enum enum_field_types>::toText(field_type),
            std::to_string(inclusiveMinimum),
            std::to_string(inclusiveMaximum)});
}


/*********************** RND ************************************************/

class RND_int : public EncLayer {
public:
    RND_int(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const {return key;}
    RND_int(unsigned int id, const std::string &serial);

    SECLEVEL level() const {return SECLEVEL::RND;}
    std::string name() const {return "RND_int";}

    Create_field * newCreateField(const Create_field * const cf,
                                  const std::string &anonname = "")
        const;

    Item *encrypt(const Item &ptext, uint64_t IV) const;
    Item * decrypt(Item * const ctext, uint64_t IV) const;
    Item * decryptUDF(Item * const col, Item * const ivcol) const;

private:
    std::string const key;
    blowfish const bf;
    static int const key_bytes = 16;
    static int const ciph_size = 8;

};

class RND_str : public EncLayer {
public:
    RND_str(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const {return rawkey;}
    RND_str(unsigned int id, const std::string &serial);

    SECLEVEL level() const {return SECLEVEL::RND;}
    std::string name() const {return "RND_str";}
    Create_field * newCreateField(const Create_field * const cf,
                                  const std::string &anonname = "")
        const;

    Item *encrypt(const Item &ptext, uint64_t IV) const;
    Item * decrypt(Item * const ctext, uint64_t IV) const;
    Item * decryptUDF(Item * const col, Item * const ivcol) const;

private:
    std::string const rawkey;
    static int const key_bytes = 16;
    const std::unique_ptr<const AES_KEY> enckey;
    const std::unique_ptr<const AES_KEY> deckey;

};

std::unique_ptr<EncLayer>
RNDFactory::create(Create_field * const cf, const std::string &key)
{
    if (isMySQLTypeNumeric(*cf)) { // the ope case as well 
        return std::unique_ptr<EncLayer>(new RND_int(cf, key));
    } else {
        return std::unique_ptr<EncLayer>(new RND_str(cf, key));
    }
}

std::unique_ptr<EncLayer>
RNDFactory::deserialize(unsigned int id, const SerialLayer &sl)
{
    if (sl.name == "RND_int") {
        return std::unique_ptr<EncLayer>(new RND_int(id, sl.layer_info));
    } else {
        return std::unique_ptr<EncLayer>(new RND_str(id, sl.layer_info));
    }
}


RND_int::RND_int(Create_field * const f, const std::string &seed_key)
    : key(prng_expand(seed_key, key_bytes)),
      bf(key)
{}

RND_int::RND_int(unsigned int id, const std::string &serial)
    : EncLayer(id), key(serial), bf(key)
{}

Create_field *
RND_int::newCreateField(const Create_field * const cf,
                        const std::string &anonname) const
{
    // MYSQL_TYPE_LONGLONG because blowfish works on 64 bit blocks.
    return createFieldHelper(cf, ciph_size, MYSQL_TYPE_LONGLONG,
                             anonname);
}

//TODO: may want to do more specialized crypto for lengths
Item *
RND_int::encrypt(const Item &ptext, uint64_t IV) const
{
    // assert(!stringItem(ptext));
    //TODO: should have encrypt_SEM work for any length
    const uint64_t p = RiboldMYSQL::val_uint(ptext);
    const uint64_t c = bf.encrypt(p ^ IV);
    LOG(encl) << "RND_int encrypt " << p << " IV " << IV << "-->" << c;

    return new (current_thd->mem_root)
               Item_int(static_cast<ulonglong>(c));
}

Item *
RND_int::decrypt(Item * const ctext, uint64_t IV) const
{
    const uint64_t c = static_cast<Item_int*>(ctext)->value;
    const uint64_t p = bf.decrypt(c) ^ IV;
    LOG(encl) << "RND_int decrypt " << c << " IV " << IV << " --> " << p;

    return new (current_thd->mem_root)
               Item_int(static_cast<ulonglong>(p));
}

static udf_func u_decRNDInt = {
    LEXSTRING("cryptdb_decrypt_int_sem"),
    INT_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};


Item *
RND_int::decryptUDF(Item * const col, Item * const ivcol) const
{
    List<Item> l;
    l.push_back(col);

    l.push_back(get_key_item(key));

    l.push_back(ivcol);

    Item *const udfdec =
        new (current_thd->mem_root) Item_func_udf_int(&u_decRNDInt, l);
    udfdec->name = NULL; //no alias

    //add encompassing CAST for unsigned
    Item *const udf =
        new (current_thd->mem_root) Item_func_unsigned(udfdec);
    udf->name = NULL;

    return udf;
}

///////////////////////////////////////////////

RND_str::RND_str(Create_field * const f, const std::string &seed_key)
    : rawkey(prng_expand(seed_key, key_bytes)),
      enckey(get_AES_enc_key(rawkey)), deckey(get_AES_dec_key(rawkey))
{}

RND_str::RND_str(unsigned int id, const std::string &serial)
    : EncLayer(id), rawkey(serial), enckey(get_AES_enc_key(rawkey)),
      deckey(get_AES_dec_key(rawkey))
{}


Create_field *
RND_str::newCreateField(const Create_field * const cf,
                        const std::string &anonname) const
{
    const auto typelen = AESTypeAndLength(*cf, false);
    return createFieldHelper(cf, typelen.second, typelen.first,
                             anonname, &my_charset_bin);
}

Item *
RND_str::encrypt(const Item &ptext, uint64_t IV) const
{
    const std::string enc =
        encrypt_AES_CBC(ItemToString(ptext), enckey.get(),
                        BytesFromInt(IV, SALT_LEN_BYTES), false);

    LOG(encl) << "RND_str encrypt " << ItemToString(ptext) << " IV "
              << IV << "--->" << "len of enc " << enc.length()
              << " enc " << enc;

    return new (current_thd->mem_root) Item_string(make_thd_string(enc),
                                                   enc.length(),
                                                   &my_charset_bin);
}

Item *
RND_str::decrypt(Item * const ctext, uint64_t IV) const
{
    const std::string dec =
        decrypt_AES_CBC(ItemToString(*ctext), deckey.get(),
                        BytesFromInt(IV, SALT_LEN_BYTES), false);
    LOG(encl) << "RND_str decrypt " << ItemToString(*ctext) << " IV "
              << IV << "-->" << "len of dec " << dec.length()
              << " dec: " << dec;

    return new (current_thd->mem_root) Item_string(make_thd_string(dec),
                                                   dec.length(),
                                                   &my_charset_bin);
}


//TODO; make edb.cc udf naming consistent with these handlers
static udf_func u_decRNDString = {
    LEXSTRING("cryptdb_decrypt_text_sem"),
    STRING_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};


Item *
RND_str::decryptUDF(Item * const col, Item * const ivcol) const
{
    List<Item> l;
    l.push_back(col);
    l.push_back(get_key_item(rawkey));
    l.push_back(ivcol);

    return new (current_thd->mem_root) Item_func_udf_str(&u_decRNDString,
                                                         l);
}


/********** DET ************************/


class DET_abstract_integer : public EncLayer {
public:
    DET_abstract_integer(Create_field *const f, const std::string &key);
    DET_abstract_integer(unsigned int id, CryptedInteger &cinteger);

    virtual std::string name() const = 0;
    virtual SECLEVEL level() const = 0;

    std::string doSerialize() const;
    template <typename Type>
        static std::unique_ptr<Type>
        deserialize(unsigned int id, const std::string &serial);

    Create_field *newCreateField(const Create_field *const cf,
                                 const std::string &anonname = "")
        const;

    // FIXME: final
    Item *encrypt(const Item &ptext, uint64_t IV) const;
    Item *decrypt(Item *const ctext, uint64_t IV) const;
    Item *decryptUDF(Item *const col, Item *const ivcol = NULL) const;

protected:
    CryptedInteger cinteger;
    const blowfish bf;
    static const int bf_key_size = 16;

private:
    std::string getKeyFromSerial(const std::string &serial);
};

/*
class DET_abstract_decimal : public DET_abstract_number {
public:
    DET_abstract_decimal(Create_field *const cf,
                         const std::string &seed_key);
    DET_abstract_decimal(unsigned int id, const std::string &serial);

    // FIXME: final.
    std::string doSerialize() const;
    Item *encrypt(const Item &ptext, uint64_t IV) const;
    Item *decrypt(Item *const ctext, uint64_t IV) const;

protected:
    const uint decimals;      // number of decimals
};
*/


class DET_int : public DET_abstract_integer {
public:
    DET_int(Create_field *const cf, const std::string &seed_key)
        : DET_abstract_integer(cf, seed_key) {}
    // create object from serialized contents
    DET_int(unsigned int id, CryptedInteger &cinteger)
        : DET_abstract_integer(id, cinteger) {}

    virtual SECLEVEL level() const {return SECLEVEL::DET;}
    std::string name() const {return "DET_int";}
};

static udf_func u_decDETInt = {
    LEXSTRING("cryptdb_decrypt_int_det"),
    INT_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};

/*
class DET_dec : public DET_abstract_decimal {
public:
    DET_dec(Create_field *const cf, const std::string &seed_key);

    // create object from serialized contents
    DET_dec(unsigned int id, const std::string &serial);

    // FIXME: final
    SECLEVEL level() const {return SECLEVEL::DET;}
    std::string name() const {return "DET_dec";}
};
*/

class DET_str : public EncLayer {
public:
    DET_str(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const {return rawkey;}
    DET_str(unsigned int id, const std::string &serial);


    virtual SECLEVEL level() const {return SECLEVEL::DET;}
    std::string name() const {return "DET_str";}
    Create_field * newCreateField(const Create_field * const cf,
                                  const std::string &anonname = "")
        const;

    Item *encrypt(const Item &ptext, uint64_t IV) const;
    Item * decrypt(Item * const ctext, uint64_t IV) const;
    Item * decryptUDF(Item * const col, Item * const ivcol = NULL) const;

protected:
    std::string const rawkey;
    static const int key_bytes = 16;
    const std::unique_ptr<const AES_KEY> enckey;
    const std::unique_ptr<const AES_KEY> deckey;

};


std::unique_ptr<EncLayer>
DETFactory::create(Create_field * const cf, const std::string &key)
{
    if (isMySQLTypeNumeric(*cf)) {
        if (cf->sql_type == MYSQL_TYPE_DECIMAL
            || cf->sql_type == MYSQL_TYPE_NEWDECIMAL) {
            FAIL_TextMessageError("decimal support is broken");
        } else {
            return std::unique_ptr<EncLayer>(new DET_int(cf, key));
        }
    } else {
        return std::unique_ptr<EncLayer>(new DET_str(cf, key));
    }
}

std::unique_ptr<EncLayer>
DETFactory::deserialize(unsigned int id, const SerialLayer &sl)
{
    if ("DET_int" == sl.name) {
        return DET_abstract_integer::deserialize<DET_int>(id,
                                                       sl.layer_info);
    } else if ("DET_dec" == sl.name) {
        FAIL_TextMessageError("decimal support broken");
    } else if ("DET_str" == sl.name) {
        return std::unique_ptr<EncLayer>(new DET_str(id, sl.layer_info));
    } else {
        FAIL_TextMessageError("Unknown type for DET deserialization!");
    }
}

DET_abstract_integer::DET_abstract_integer(Create_field *const cf,
                                           const std::string &key)
    : EncLayer(), cinteger(CryptedInteger(*cf, key)), bf(key)
{}

DET_abstract_integer::DET_abstract_integer(unsigned int id,
                                           CryptedInteger &cinteger)
    : EncLayer(id), cinteger(cinteger), bf(cinteger.getKey())
{}

template <typename Type>
std::unique_ptr<Type>
DET_abstract_integer::deserialize(unsigned int id,
                                  const std::string &serial)
{
    /* if the concrete DET integer classes need to serialize data that
     * is not in CryptedInteger; write a deserialize function for them
     * as well and let them handle the serialized data that is not
     * CryptedInteger */
    CryptedInteger cint = CryptedInteger::deserialize(serial);
    return std::unique_ptr<Type>(new Type(id, cint));
}

std::string
DET_abstract_integer::doSerialize() const
{
    return cinteger.serialize();
}

Create_field *
DET_abstract_integer::newCreateField(const Create_field * const cf,
                                     const std::string &anonname) const
{
    const int64_t ciph_size = 8;
    // MYSQL_TYPE_LONGLONG because blowfish works on 64 bit blocks.
    return createFieldHelper(cf, ciph_size, MYSQL_TYPE_LONGLONG,
                             anonname);
}

Item *
DET_abstract_integer::encrypt(const Item &ptext, uint64_t IV) const
{
    // assert(!stringItem(ptext));
    const ulonglong value = RiboldMYSQL::val_uint(ptext);

    const ulonglong res = static_cast<ulonglong>(bf.encrypt(value));
    LOG(encl) << "DET_int enc " << value << "--->" << res;
    return new (current_thd->mem_root) Item_int(res);
}

Item *
DET_abstract_integer::decrypt(Item *const ctext, uint64_t IV) const
{
    const ulonglong value = static_cast<Item_int *>(ctext)->value;
    const ulonglong retdec = bf.decrypt(value);
    LOG(encl) << "DET_int dec " << value << "--->" << retdec;
    return new (current_thd->mem_root) Item_int(retdec);
}

Item *
DET_abstract_integer::decryptUDF(Item *const col, Item *const ivcol)
    const
{
    List<Item> l;
    l.push_back(col);

    l.push_back(get_key_item(cinteger.getKey()));

    Item *const udfdec = new Item_func_udf_int(&u_decDETInt, l);
    udfdec->name = NULL;

    Item *const udf = new Item_func_unsigned(udfdec);
    udf->name = NULL;

    return udf;
}

std::string
DET_abstract_integer::getKeyFromSerial(const std::string &serial)
{
    return serial.substr(serial.find(' ')+1, std::string::npos);
}

/*
DET_abstract_decimal::DET_abstract_decimal(Create_field *const cf,
                                           const std::string &seed_key)
    : DET_abstract_number(seed_key, pow(10, cf->decimals)),
      decimals(cf->decimals)
{
    // > make sure we have at most 8 precision
    // > a number with form DECIMAL(a, b) has decimals = b
    //   and length = a + b
    assert_s(cf->length - cf->decimals <= 8,
             " this type of decimal not supported ");

    // decimals = cf->decimals;
    // shift = pow(10, decimals);
}

DET_abstract_decimal::DET_abstract_decimal(unsigned int id,
                                           const std::string &serial)
    : DET_abstract_number(id, underSerial(serial)),
      decimals(getDecimals(serial))
{}

std::string
DET_abstract_decimal::doSerialize() const
{
    return std::to_string(decimals) + " " +
            DET_abstract_number::doSerialize();
}

static Item_int *
decimal_to_int(const Item_decimal &v, uint decimals,
               const ulonglong &shift)
{
    const ulonglong res = RiboldMYSQL::val_real(v) * shift;

    return new (current_thd->mem_root) Item_int(res);
}

Item *DET_abstract_decimal::encrypt(const Item &ptext, uint64_t IV) const
{
    const Item_decimal &ptext_dec =
        static_cast<const Item_decimal &>(ptext);
    const std::unique_ptr<Item_int>
        ptext_int(decimal_to_int(ptext_dec, decimals, shift));
    return DET_abstract_number::encrypt(*ptext_int, IV);
}

Item *DET_abstract_decimal::decrypt(Item *const ctext, uint64_t IV) const
{
    const std::unique_ptr<Item_int>
        res_int(static_cast<Item_int*>(DET_abstract_number::decrypt(ctext,
                                                                    IV)));
    Item_decimal * const res =
        new (current_thd->mem_root) Item_decimal(res_int->value*1.0/shift,
                                                 decimals,
                                                 decimals);
    LOG(encl) << "DET_dec dec " << res_int->value << "--->"
              << RiboldMYSQL::val_real(*res) << std::endl;

    return res;
}

DET_dec::DET_dec(Create_field * const cf, const std::string &seed_key)
    : DET_abstract_decimal(cf, seed_key)
{}

DET_dec::DET_dec(unsigned int id, const std::string &serial)
    : DET_abstract_decimal(id, serial)
{}
*/

DET_str::DET_str(Create_field * const f, const std::string &seed_key)
    : rawkey(prng_expand(seed_key, key_bytes)),
      enckey(get_AES_enc_key(rawkey)), deckey(get_AES_dec_key(rawkey))
{}

DET_str::DET_str(unsigned int id, const std::string &serial)
    : EncLayer(id), rawkey(serial), enckey(get_AES_enc_key(rawkey)),
    deckey(get_AES_dec_key(rawkey))
{}


Create_field *
DET_str::newCreateField(const Create_field * const cf,
                        const std::string &anonname) const
{
    const auto typelen = AESTypeAndLength(*cf, true);
    return createFieldHelper(cf, typelen.second, typelen.first, anonname,
                             &my_charset_bin);
}

Item *
DET_str::encrypt(const Item &ptext, uint64_t IV) const
{
    const std::string plain = ItemToString(ptext);
    const std::string enc = encrypt_AES_CMC(plain, enckey.get(), true);
    LOG(encl) << " DET_str encrypt " << plain  << " IV " << IV << " ---> "
              << " enc len " << enc.length() << " enc " << enc;

    return new (current_thd->mem_root) Item_string(make_thd_string(enc),
                                                   enc.length(),
                                                   &my_charset_bin);
}

Item *
DET_str::decrypt(Item * const ctext, uint64_t IV) const
{
    const std::string enc = ItemToString(*ctext);
    const std::string dec = decrypt_AES_CMC(enc, deckey.get(), true);
    LOG(encl) << " DET_str decrypt enc len " << enc.length()
              << " enc " << enc << " IV " << IV << " ---> "
              << " dec len " << dec.length() << " dec " << dec;

    return new (current_thd->mem_root) Item_string(make_thd_string(dec),
                                                   dec.length(),
                                                   &my_charset_bin);
}

static udf_func u_decDETStr = {
    LEXSTRING("cryptdb_decrypt_text_det"),
    STRING_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};



Item *
DET_str::decryptUDF(Item * const col, Item * const ivcol) const
{
    List<Item> l;
    l.push_back(col);
    l.push_back(get_key_item(rawkey));
    return new (current_thd->mem_root) Item_func_udf_str(&u_decDETStr, l);

}

/*************** DETJOIN *********************/


class DETJOIN_int : public DET_abstract_integer {
public:
    DETJOIN_int(Create_field *const cf, const std::string &seed_key)
        : DET_abstract_integer(cf, seed_key) {}
    // serialize from parent;  unserialize:
    DETJOIN_int(unsigned int id, CryptedInteger &cinteger)
        : DET_abstract_integer(id, cinteger) {}

    SECLEVEL level() const {return SECLEVEL::DETJOIN;}
    std::string name() const {return "DETJOIN_int";}
};

class DETJOIN_str : public DET_str {
public:
    DETJOIN_str(Create_field * const cf, const std::string &seed_key)
        : DET_str(cf, seed_key) {}

    // serialize from parent; unserialize:
    DETJOIN_str(unsigned int id, const std::string &serial)
        : DET_str(id, serial) {};

    SECLEVEL level() const {return SECLEVEL::DETJOIN;}
    std::string name() const {return "DETJOIN_str";}
private:
 
};

/*
class DETJOIN_dec : public DET_abstract_decimal {
    //TODO
public:
    DETJOIN_dec(Create_field *const cf, const std::string &seed_key)
        : DET_abstract_decimal(cf, seed_key) {}

    // serialize from parent;  unserialize:
    DETJOIN_dec(unsigned int id, const std::string &serial)
        : DET_abstract_decimal(id, serial) {}

    // FIXME: final
    SECLEVEL level() const {return SECLEVEL::DETJOIN;}
    std::string name() const {return "DETJOIN_dec";}
};
*/

std::unique_ptr<EncLayer>
DETJOINFactory::create(Create_field * const cf,
                       const std::string &key)
{
    if (isMySQLTypeNumeric(*cf)) {
        if (cf->sql_type == MYSQL_TYPE_DECIMAL
            || cf->sql_type == MYSQL_TYPE_NEWDECIMAL) {
            FAIL_TextMessageError("decimal support is broken");
        } else {
            return std::unique_ptr<EncLayer>(new DETJOIN_int(cf, key));
        }
    } else {
        return std::unique_ptr<EncLayer>(new DETJOIN_str(cf, key));
    }
}

std::unique_ptr<EncLayer>
DETJOINFactory::deserialize(unsigned int id, const SerialLayer &sl)
{
    if  ("DETJOIN_int" == sl.name) {
        return DET_abstract_integer::deserialize<DETJOIN_int>(id,
                                                    sl.layer_info);
    } else if ("DETJOIN_dec" == sl.name) {
        FAIL_TextMessageError("decimal support broken");
    } else if ("DETJOIN_str" == sl.name) {
        return std::unique_ptr<EncLayer>(new DETJOIN_str(id,
                                                         sl.layer_info));
    } else {
        FAIL_TextMessageError("DETJOINFactory does not recognize type!");
    }
}



/**************** OPE **************************/


class OPE_int : public EncLayer {
public:
    OPE_int(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const {return key;}
    OPE_int(unsigned int id, const std::string &serial);
  
    SECLEVEL level() const {return SECLEVEL::OPE;}
    std::string name() const {return "OPE_int";}
    Create_field * newCreateField(const Create_field * const cf,
                                  const std::string &anonname = "")
        const;

    Item *encrypt(const Item &p, uint64_t IV) const;
    Item * decrypt(Item * const c, uint64_t IV) const;


private:
    std::string const key;
    // HACK.
    mutable OPE ope;
    static const size_t key_bytes = 16;
    static const size_t plain_size = 4;
    static const size_t ciph_size = 8;
};

class OPE_str : public EncLayer {
public:
    OPE_str(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const {return key;}
    OPE_str(unsigned int id, const std::string &serial);

    SECLEVEL level() const {return SECLEVEL::OPE;}
    std::string name() const {return "OPE_str";}
    Create_field * newCreateField(const Create_field * const cf,
                                  const std::string &anonname = "")
        const;

    Item *encrypt(const Item &p, uint64_t IV) const;
    Item * decrypt(Item * const c, uint64_t IV) const
        __attribute__((noreturn));

private:
    std::string const key;
    // HACK.
    mutable OPE ope;
    static const size_t key_bytes = 16;
    static const size_t plain_size = 4;
    static const size_t ciph_size = 8;
};


/*
class OPE_dec : public OPE_int {
public:
    OPE_dec(Create_field * const cf, const std::string &seed_key);

    // serialize and deserialize
    std::string doSerialize() const;
    OPE_dec(unsigned int id, const std::string &serial);

    std::string name() const {return "OPE_dec";}

    Item *encrypt(const Item &p, uint64_t IV) const;
    Item * decrypt(Item * const c, uint64_t IV) const;

private:
    uint const decimals;
    ulonglong const shift;
};
*/



std::unique_ptr<EncLayer>
OPEFactory::create(Create_field * const cf, const std::string &key)
{
    if (isMySQLTypeNumeric(*cf)) { 
        if (cf->sql_type == MYSQL_TYPE_DECIMAL
            || cf->sql_type ==  MYSQL_TYPE_NEWDECIMAL) {
            FAIL_TextMessageError("decimal support is broken");
        }
        return std::unique_ptr<EncLayer>(new OPE_int(cf, key));
    }
    return std::unique_ptr<EncLayer>(new OPE_str(cf, key));
}

std::unique_ptr<EncLayer>
OPEFactory::deserialize(unsigned int id, const SerialLayer &sl)
{
    if (sl.name == "OPE_int") {
        return std::unique_ptr<EncLayer>(new OPE_int(id,
                                                     sl.layer_info));
    } else if (sl.name == "OPE_str") {
        return std::unique_ptr<EncLayer>(new OPE_str(id, sl.layer_info));
    } else {
        FAIL_TextMessageError("decimal support broken");
    }
}

/*
OPE_dec::OPE_dec(Create_field * const cf, const std::string &seed_key)
    : OPE_int(cf, seed_key), decimals(cf->decimals),
      shift(pow(10, decimals))
{
    assert_s(cf->length - cf->decimals <= 8,
             "this type of decimal not supported ");
}

std::string
OPE_dec::doSerialize() const
{
    return std::to_string(decimals) + " " + OPE_int::doSerialize();
}

OPE_dec::OPE_dec(unsigned int id, const std::string &serial)
    : OPE_int(id, underSerial(serial)), decimals(getDecimals(serial)),
      shift(pow(10, decimals))
{}

Item *
OPE_dec::encrypt(const Item &ptext, uint64_t IV) const
{
    const Item_decimal &ptext_dec =
        static_cast<const Item_decimal &>(ptext);
    const std::unique_ptr<Item_int>
        ptext_int(decimal_to_int(ptext_dec, decimals, shift));
    return OPE_int::encrypt(*ptext_int.get(), IV);
}


Item *
OPE_dec::decrypt(Item * const ctext, uint64_t IV) const
{
    const std::unique_ptr<Item_int>
        res_int(static_cast<Item_int *>(OPE_int::decrypt(ctext, IV)));

    Item_decimal * const res =
        new (current_thd->mem_root) Item_decimal(res_int->value*1.0/shift,
                                                 decimals,
                                                 decimals);

    return res;
}
*/


OPE_int::OPE_int(Create_field * const f, const std::string &seed_key)
    : key(prng_expand(seed_key, key_bytes)),
      ope(OPE(key, plain_size * 8, ciph_size * 8))
{}

OPE_int::OPE_int(unsigned int id, const std::string &serial)
    : EncLayer(id), key(serial),
      ope(OPE(key, plain_size * 8, ciph_size * 8))
{}

Create_field *
OPE_int::newCreateField(const Create_field * const cf,
                        const std::string &anonname) const
{
    return createFieldHelper(cf, cf->length, MYSQL_TYPE_LONGLONG,
                             anonname);
}

Item *
OPE_int::encrypt(const Item &ptext, uint64_t IV) const
{
    // assert(!stringItem(ptext));
    // AWARE: Truncation.
    const uint32_t pval = RiboldMYSQL::val_uint(ptext);
    const ulonglong enc = uint64FromZZ(ope.encrypt(to_ZZ(pval)));
    LOG(encl) << "OPE_int encrypt " << pval << " IV " << IV
              << "--->" << enc;

    return new (current_thd->mem_root) Item_int(enc);
}

Item *
OPE_int::decrypt(Item * const ctext, uint64_t IV) const
{
    const ulonglong cval =
        static_cast<ulonglong>(static_cast<Item_int *>(ctext)->value);
    const ulonglong dec = uint64FromZZ(ope.decrypt(ZZFromUint64(cval)));
    LOG(encl) << "OPE_int decrypt " << cval << " IV " << IV
              << "--->" << dec << std::endl;

    return new (current_thd->mem_root) Item_int(dec);
}


OPE_str::OPE_str(Create_field * const f, const std::string &seed_key)
    : key(prng_expand(seed_key, key_bytes)),
      ope(OPE(key, plain_size * 8, ciph_size * 8))
{}

OPE_str::OPE_str(unsigned int id, const std::string &serial)
    : EncLayer(id), key(serial),
    ope(OPE(key, plain_size * 8, ciph_size * 8))
{}

Create_field *
OPE_str::newCreateField(const Create_field * const cf,
                        const std::string &anonname) const
{
    return createFieldHelper(cf, cf->length, MYSQL_TYPE_LONGLONG,
                             anonname, &my_charset_bin);
}

/*
 * Make all characters uppercase as mysql string order comparison
 * is case insensitive.
 *
 * mysql> SELECT 'a' = 'a', 'A' = 'a', 'A' < 'b', 'z' > 'M';
 * +-----------+-----------+-----------+-----------+
 * | 'a' = 'a' | 'A' = 'a' | 'A' < 'b' | 'z' > 'M' |
 * +-----------+-----------+-----------+-----------+
 * |         1 |         1 |         1 |         1 |
 * +-----------+-----------+-----------+-----------+
 */
Item *
OPE_str::encrypt(const Item &ptext, uint64_t IV) const
{
    std::string ps = toUpperCase(ItemToString(ptext));
    if (ps.size() < plain_size)
        ps = ps + std::string(plain_size - ps.size(), 0);

    uint32_t pv = 0;

    for (uint i = 0; i < plain_size; i++) {
        pv = pv * 256 + static_cast<int>(ps[i]);
    }

    const ZZ enc = ope.encrypt(to_ZZ(pv));

    return new (current_thd->mem_root)
               Item_int(static_cast<ulonglong>(uint64FromZZ(enc)));
}

Item *
OPE_str::decrypt(Item * const ctext, uint64_t IV) const
{
    thrower() << "cannot decrypt string from OPE";
}


/**************** HOM ***************************/

/*
class HOM_dec : public HOM {
public:
    HOM_dec(Create_field * const cf, const std::string &seed_key);

    //deserialize
    HOM_dec(unsigned int id, const std::string &serial);
    ~HOM_dec() {;}

    std::string name() const {return "HOM_dec";}


    //TODO needs multi encrypt and decrypt
    Item *encrypt(const Item &p, uint64_t IV) const;
    Item * decrypt(Item * const c, uint64_t IV) const;

    //expr is the expression (e.g. a field) over which to sum
    Item *sumUDA(Item *const expr) const;
    Item *sumUDF(Item *const i1, Item *const i2) const;

private:
    uint const decimals;
    ZZ const shift;

    std::string doSerialize() const;
};
*/


std::unique_ptr<EncLayer>
HOMFactory::create(Create_field * const cf, const std::string &key)
{
    if (cf->sql_type == MYSQL_TYPE_DECIMAL
        || cf->sql_type == MYSQL_TYPE_NEWDECIMAL) {
        FAIL_TextMessageError("decimal support is broken");
    }

    return std::unique_ptr<EncLayer>(new HOM(cf, key));
}

std::unique_ptr<EncLayer>
HOMFactory::deserialize(unsigned int id, const SerialLayer &serial)
{
    if (serial.name == "HOM_dec") {
        FAIL_TextMessageError("decimal support broken");
    }
    return std::unique_ptr<EncLayer>(new HOM(id, serial.layer_info));
}

static ZZ
ItemIntToZZ(const Item &ptext)
{
    const ulonglong val = RiboldMYSQL::val_uint(ptext);
    return ZZFromUint64(val);
}

static Item *
ZZToItemInt(const ZZ &val)
{
    const ulonglong v = uint64FromZZ(val);
    return new (current_thd->mem_root) Item_int(v);
}

static Item *
ZZToItemStr(const ZZ &val)
{
    const std::string str = StringFromZZ(val);
    Item * const newit =
        new (current_thd->mem_root) Item_string(make_thd_string(str),
                                                str.length(),
                        &my_charset_bin);
    newit->name = NULL; //no alias

    return newit;
}

static ZZ
ItemStrToZZ(Item *const i)
{
    const std::string res = ItemToString(*i);
    return ZZFromString(res);
}

/*
HOM_dec::HOM_dec(Create_field * const cf, const std::string &seed_key)
    : HOM(cf, seed_key), decimals(cf->decimals),
      shift(power(to_ZZ(10), decimals))
{
    assert_s(cf->length <= 120, "too large decimal for HOM layer");
}

std::string
HOM_dec::doSerialize() const
{
    return std::to_string(decimals) + " " + HOM::doSerialize();
}

HOM_dec::HOM_dec(unsigned int id, const std::string &serial)
    : HOM(id, underSerial(serial)), decimals(getDecimals(serial)),
      shift(power(to_ZZ(10), decimals))
{}

static ZZ
ItemDecToZZ(const Item &ptext, const ZZ &shift, uint decimals)
{
    bool is_null;
    // ss is a number
    // : - xxxx.yyyy
    const std::string &ss(RiboldMYSQL::val_str(ptext, &is_null));
    assert(false == is_null);

    std::string ss_int = ss.substr(0, ss.find('.')); // integer part
    if (ss_int == "") ss_int = "0";
    std::string ss_dec = "";
    if (ss.find('.') != std::string::npos) {
        ss_dec = ss.substr(ss.find('.') + 1); // decimal part
    }

    const uint actual_decs = ss_dec.length();
    assert_s(actual_decs <= decimals, "value has more decimals than declared");

    ZZ val_int = ZZFromDecString(ss_int);
    ZZ val_dec = ZZFromDecString(ss_dec);

    // make an integer out of it   
    val_dec = val_dec * power(to_ZZ(10), decimals - actual_decs);
    
    return val_int * shift + val_dec;
}

static Item_decimal *
ZZToItemDec(const ZZ &val, const ZZ &shift)
{
    const ZZ val_int = val / shift;
    const ZZ val_dec = val % shift;

    const std::string num =
        DecStringFromZZ(val_int) + "." + DecStringFromZZ(val_dec);
    
    return new (current_thd->mem_root) Item_decimal(num.data(),
                                                    num.length(),
                                                    &my_charset_numeric);
}


Item *
HOM_dec::encrypt(const Item &ptext, uint64_t IV) const
{
    const ZZ enc = sk->encrypt(ItemDecToZZ(ptext, shift, decimals));

    return ZZToItemStr(enc);
}

Item *
HOM_dec::decrypt(Item * const ctext, uint64_t IV) const
{
    const ZZ enc = ItemStrToZZ(ctext);
    const ZZ dec = sk->decrypt(enc);

    return ZZToItemDec(dec, shift);
}
*/



HOM::HOM(Create_field * const f, const std::string &seed_key)
    : seed_key(seed_key), sk(NULL), waiting(true)
{}

HOM::HOM(unsigned int id, const std::string &serial)
    : EncLayer(id), seed_key(serial), sk(NULL), waiting(true)
{}

Create_field *
HOM::newCreateField(const Create_field * const cf,
                    const std::string &anonname) const
{
    return createFieldHelper(cf, 2*nbits/8, MYSQL_TYPE_VARCHAR,
                             anonname, &my_charset_bin);
}

void
HOM::unwait() const
{
    const std::unique_ptr<streamrng<arc4>>
        prng(new streamrng<arc4>(seed_key));
    sk = new Paillier_priv(Paillier_priv::keygen(prng.get(), nbits));
    waiting = false;
}

Item *
HOM::encrypt(const Item &ptext, uint64_t IV) const
{
    if (true == waiting) {
        this->unwait();
    }

    const ZZ enc = sk->encrypt(ItemIntToZZ(ptext));
    return ZZToItemStr(enc);
}

Item *
HOM::decrypt(Item * const ctext, uint64_t IV) const
{
    if (true == waiting) {
        this->unwait();
    }

    const ZZ enc = ItemStrToZZ(ctext);
    const ZZ dec = sk->decrypt(enc);
    LOG(encl) << "HOM ciph " << enc << "---->" << dec;
    return ZZToItemInt(dec);
}

static udf_func u_sum_a = {
    LEXSTRING("cryptdb_agg"),
    STRING_RESULT,
    UDFTYPE_AGGREGATE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};

static udf_func u_sum_f = {
    LEXSTRING("cryptdb_func_add_set"),
    STRING_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};

Item *
HOM::sumUDA(Item *const expr) const
{
    if (true == waiting) {
        this->unwait();
    }

    List<Item> l;
    l.push_back(expr);
    l.push_back(ZZToItemStr(sk->hompubkey()));
    return new (current_thd->mem_root) Item_func_udf_str(&u_sum_a, l);
}

Item *
HOM::sumUDF(Item *const i1, Item *const i2) const
{
    if (true == waiting) {
        this->unwait();
    }

    List<Item> l;
    l.push_back(i1);
    l.push_back(i2);
    l.push_back(ZZToItemStr(sk->hompubkey()));

    return new (current_thd->mem_root) Item_func_udf_str(&u_sum_f, l);
}

HOM::~HOM() {
    delete sk;
}

/******* SEARCH **************************/

Search::Search(Create_field * const f, const std::string &seed_key)
    : key(prng_expand(seed_key, key_bytes))
{}

Search::Search(unsigned int id, const std::string &serial) 
    : EncLayer(id), key(prng_expand(serial, key_bytes))
{}

Create_field *
Search::newCreateField(const Create_field * const cf,
                       const std::string &anonname) const
{
    return createFieldHelper(cf, cf->length, MYSQL_TYPE_BLOB, anonname,
                             &my_charset_bin);
}


//returns the concatenation of all words in the given list
static std::string
assembleWords(std::list<std::string> * const words)
{
    std::string res = "";

    for (std::list<std::string>::iterator it = words->begin();
         it != words->end();
         it++) {
        res = res + *it;
    }

    return res;
}

static std::string
encryptSWP(const std::string &key, const std::list<std::string> &words)
{
    const std::unique_ptr<std::list<std::string>>
        l(SWP::encrypt(key, words));
    const std::string r = assembleWords(l.get());
    return r;
}

static Token
token(const std::string &key, const std::string &word)
{
    return SWP::token(key, word);
}

//this function should in fact be provided by the programmer
//currently, we split by whitespaces
// only consider words at least 3 chars in len
// discard not unique objects
static std::list<std::string> *
tokenize(const std::string &text)
{
    std::list<std::string> tokens = split(text, " ,;:.");

    std::set<std::string> search_tokens;

    std::list<std::string> * const res = new std::list<std::string>();

    for (std::list<std::string>::iterator it = tokens.begin();
            it != tokens.end();
            it++) {
        if ((it->length() >= 3) &&
                (search_tokens.find(*it) == search_tokens.end())) {
            const std::string token = toLowerCase(*it);
            search_tokens.insert(token);
            res->push_back(token);
        }
    }

    search_tokens.clear();
    return res;

}

static char *
newmem(const std::string &a)
{
    const unsigned int len = a.length();
    char * const res = new char[len];
    memcpy(res, a.c_str(), len);
    return res;
}

Item *
Search::encrypt(const Item &ptext, uint64_t IV) const
{
    const std::string plainstr = ItemToString(ptext);
    //TODO: remove string, string serves this purpose now..
    const std::list<std::string> * const tokens = tokenize(plainstr);
    const std::string ciph = encryptSWP(key, *tokens);

    LOG(encl) << "SEARCH encrypt " << plainstr << " --> " << ciph;

    return new Item_string(newmem(ciph), ciph.length(), &my_charset_bin);
}

Item *
Search::decrypt(Item * const ctext, uint64_t IV) const
{
    thrower() << "decryption from SWP not supported \n";
}

static udf_func u_search = {
    LEXSTRING("cryptdb_searchSWP"),
    INT_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};


static std::string
searchstrip(std::string s)
{
    if (s[0] == '%') {
        s = s.substr(1, s.length() - 1);
    }
    const uint len = s.length();
    if (s[len-1] == '%') {
        s = s.substr(0, len-1);
    }

    return s;
}

Item *
Search::searchUDF(Item * const field, Item * const expr) const
{
    List<Item> l = List<Item>();

    l.push_back(field);

    // Add token
    const Token t =
        token(key, std::string(searchstrip(ItemToString(*expr))));
    Item_string * const t1 =
        new Item_string(newmem(t.ciph), t.ciph.length(),
                        &my_charset_bin);
    t1->name = NULL; //no alias
    l.push_back(t1);

    Item_string * const t2 =
        new Item_string(newmem(t.wordKey), t.wordKey.length(),
                        &my_charset_bin);
    t2->name = NULL;
    l.push_back(t2);

    return new Item_func_udf_int(&u_search, l);
}

Create_field *
PlainText::newCreateField(const Create_field * const cf,
                          const std::string &anonname) const
{
    const THD * const thd = current_thd;
    Create_field * const f0 = cf->clone(thd->mem_root);
    if (anonname.size() > 0) {
        f0->field_name = make_thd_string(anonname);
    }

    return f0;
}

Item *
PlainText::encrypt(const Item &ptext, uint64_t IV) const
{
    return dup_item(ptext);
}

Item *
PlainText::decrypt(Item *const ctext, uint64_t IV) const
{
    return dup_item(*ctext);
}

Item *
PlainText::decryptUDF(Item * const col, Item * const ivcol) const
{
    FAIL_TextMessageError("No PLAIN decryption UDF");
}

std::string
PlainText::doSerialize() const
{
    return std::string("");
}

static udf_func u_cryptdb_version = {
    LEXSTRING("cryptdb_version"),
    STRING_RESULT,
    UDFTYPE_FUNCTION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0L,
};

const std::vector<udf_func*> udf_list = {
    &u_decRNDInt,
    &u_decRNDString,
    &u_decDETInt,
    &u_decDETStr,
    &u_sum_f,
    &u_sum_a,
    &u_search,
    &u_cryptdb_version
};

