#pragma once

#include <assert.h>
#include <memory>

class AbstractMySQLTypeMetaData {
    AbstractMySQLTypeMetaData(const AbstractMySQLTypeMetaData &a) = delete;
    AbstractMySQLTypeMetaData(AbstractMySQLTypeMetaData &&a) = delete;
    AbstractMySQLTypeMetaData &
        operator=(const AbstractMySQLTypeMetaData &a) = delete;
    AbstractMySQLTypeMetaData &
        operator=(AbstractMySQLTypeMetaData &&a) = delete;

public:
    AbstractMySQLTypeMetaData() {}
    ~AbstractMySQLTypeMetaData() {}

    virtual bool encryptionSupported() const = 0;
    virtual bool isNumeric() const = 0;
    virtual const std::string humanReadable(const Create_field &f)
        const = 0;
    virtual std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const = 0;
    virtual Item *intoItem(const std::string &value) const = 0;

    friend std::map<enum enum_field_types,
                    std::unique_ptr<AbstractMySQLTypeMetaData> >
        buildMySQLTypeMetaData();
};

template <enum enum_field_types id>
class MySQLTypeMetaData : public AbstractMySQLTypeMetaData {
public:
    MySQLTypeMetaData() {}
    ~MySQLTypeMetaData() {}
};

const std::string MySQLTypeToText(const Create_field &f);
bool encryptionSupported(const Create_field &f);
bool isMySQLTypeNumeric(const Create_field &f);
bool isMySQLTypeNumeric(enum enum_field_types type);
Item *MySQLFieldTypeToItem(enum enum_field_types type,
                           const std::string &value);
std::pair<enum enum_field_types, unsigned long>
AESTypeAndLength(const Create_field &f, bool pad);

// ########################################
// ########################################
//             integer types
// ########################################
// ########################################
template <enum enum_field_types id>
class MySQLIntegerMetaData : public MySQLTypeMetaData<id> {
public:
    bool encryptionSupported() const {return true;}
    bool isNumeric() const {return true;}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const
        { assert(false); }
    Item *intoItem(const std::string &value) const;
};

class MySQLTinyMetaData : public MySQLIntegerMetaData<MYSQL_TYPE_TINY> {
public:
    MySQLTinyMetaData() {}
    const std::string humanReadable(const Create_field &) const
        {return "TINYINT";}
};

class MySQLShortMetaData : public MySQLIntegerMetaData<MYSQL_TYPE_SHORT> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "SMALLINT";}
};

class MySQLInt24MetaData : public MySQLIntegerMetaData<MYSQL_TYPE_INT24> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "MEDIUMINT";}
};

class MySQLLongMetaData : public MySQLIntegerMetaData<MYSQL_TYPE_LONG> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "INT";}
};

class MySQLLongLongMetaData :
    public MySQLIntegerMetaData<MYSQL_TYPE_LONGLONG>
{
public:
    const std::string humanReadable(const Create_field &) const
        {return "BIGINT";}
};


// ########################################
// ########################################
//          fixed-point types
// ########################################
// ########################################
template <enum enum_field_types id>
class AbstractMySQLDecimalMetaData : public MySQLTypeMetaData<id> {
public:
    bool encryptionSupported() const {return true;}
    bool isNumeric() const {return true;}
    const std::string humanReadable(const Create_field &) const
        {return "DECIMAL";}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const
        { assert(false); }
    Item *intoItem(const std::string &value) const;
};

class MySQLDecimalMetaData :
    public AbstractMySQLDecimalMetaData<MYSQL_TYPE_DECIMAL> {};

class MySQLNewDecimalMetaData :
    public AbstractMySQLDecimalMetaData<MYSQL_TYPE_NEWDECIMAL> {};


// ########################################
// ########################################
//              float types
// ########################################
// ########################################
template <enum enum_field_types id>
class AbstractMySQLFloatMetaData : public MySQLTypeMetaData<id> {
public:
    bool encryptionSupported() const {return false;}
    bool isNumeric() const {return true;}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const
        { assert(false); }
    Item *intoItem(const std::string &value) const;
};

class MySQLFloatMetaData :
    public AbstractMySQLFloatMetaData<MYSQL_TYPE_FLOAT> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "FLOAT";}
};

class MySQLDoubleMetaData :
    public AbstractMySQLFloatMetaData<MYSQL_TYPE_DOUBLE> {
public:
    const std::string humanReadable(const Create_field &f) const;
};


// ########################################
// ########################################
//              string types
// ########################################
// ########################################
template <enum enum_field_types id>
class AbstractMySQLStringMetaData : public MySQLTypeMetaData<id> {
public:
    AbstractMySQLStringMetaData() {}

    bool encryptionSupported() const {return true;}
    bool isNumeric() const {return false;}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const;
    Item *intoItem(const std::string &value) const;

};

class MySQLVarCharMetaData :
    public AbstractMySQLStringMetaData<MYSQL_TYPE_VARCHAR> {
public:
    const std::string humanReadable(const Create_field &) const;
};

class MySQLVarStringMetaData :
    public AbstractMySQLStringMetaData<MYSQL_TYPE_VAR_STRING> {
public:
    const std::string humanReadable(const Create_field &) const
        { assert(false); }
};

class MySQLStringMetaData :
    public AbstractMySQLStringMetaData<MYSQL_TYPE_STRING> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "CHAR";}
};


// ########################################
// ########################################
//              date types
// ########################################
// ########################################
template <enum enum_field_types id>
class AbstractMySQLDateMetaData : public MySQLTypeMetaData<id> {
public:
    AbstractMySQLDateMetaData() {}

    bool encryptionSupported() const {return false;}
    bool isNumeric() const {return false;}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const;
    Item *intoItem(const std::string &value) const;
};

class MySQLDateMetaData :
    public AbstractMySQLDateMetaData<MYSQL_TYPE_DATE> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "DATE";}
};

class MySQLNewDateMetaData :
    public AbstractMySQLDateMetaData<MYSQL_TYPE_NEWDATE> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "DATE";}
};

class MySQLTimeMetaData :
    public AbstractMySQLDateMetaData<MYSQL_TYPE_TIME> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "TIME";}
};

class MySQLDateTimeMetaData :
    public AbstractMySQLDateMetaData<MYSQL_TYPE_DATETIME> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "DATETIME";}
};
class MySQLTimeStampMetaData :
    public AbstractMySQLDateMetaData<MYSQL_TYPE_TIMESTAMP> {
public:
    const std::string humanReadable(const Create_field &) const
        {return "TIMESTAMP";}
};


// ########################################
// ########################################
//               enum types
// ########################################
// ########################################
class MySQLEnumMetaData : public MySQLTypeMetaData<MYSQL_TYPE_ENUM> {
public:
    MySQLEnumMetaData() {}

    bool encryptionSupported() const {return false;}
    bool isNumeric() const {return false;}
    virtual const std::string humanReadable(const Create_field &f) const
        {return "ENUM";}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const
        { assert(false); }
    Item *intoItem(const std::string &value) const;
};


// ########################################
// ########################################
//               blob types
// ########################################
// ########################################
template <enum enum_field_types id>
class AbstractMySQLBlobMetaData : public MySQLTypeMetaData<id> {
public:
    bool encryptionSupported() const {return true;}
    bool isNumeric() const {return false;}
    std::pair<enum enum_field_types, unsigned long>
        AESTypeAndLength(unsigned long len, bool pad) const;
    Item *intoItem(const std::string &value) const;
};

class MySQLTinyBlobMetaData :
    public AbstractMySQLBlobMetaData<MYSQL_TYPE_TINY_BLOB>
{
public:
    const std::string humanReadable(const Create_field &) const;
};

class MySQLMediumBlobMetaData :
    public AbstractMySQLBlobMetaData<MYSQL_TYPE_MEDIUM_BLOB>
{
public:
    const std::string humanReadable(const Create_field &) const;
};

class MySQLBlobMetaData :
    public AbstractMySQLBlobMetaData<MYSQL_TYPE_BLOB>
{
public:
    const std::string humanReadable(const Create_field &) const;
};

class MySQLLongBlobMetaData :
    public AbstractMySQLBlobMetaData<MYSQL_TYPE_LONG_BLOB>
{
public:
    const std::string humanReadable(const Create_field &) const;
};

