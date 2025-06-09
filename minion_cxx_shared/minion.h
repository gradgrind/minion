#ifndef MINION_H
#define MINION_H

#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

/* The parser, Reader::read returns a single MValue. If there is an
 * error, a MinionError exception will be thrown internally and caught in
 * Reader::read, which then returns a special MValue with the error
 * message.
 */

namespace minion {

enum minion_type { T_NoType = 0, T_String, T_List, T_Map, T_Error };

class MinionError : public std::runtime_error
{
public:
	// Using constructor for passing custom message
	MinionError(const std::string& message)
        : runtime_error(message) {}
};

class MString;
class MList;
class MMap;
struct MError
{
    std::string message;

    MError(
        MinionError& e)
        : message{e.what()}
    {}
};

class Reader;

using _MV = std::variant<std::monostate,
                         std::shared_ptr<MString>,
                         std::shared_ptr<MList>,
                         std::shared_ptr<MMap>,
                         std::shared_ptr<MError>>;

class MValue : _MV
{
public:
    MValue()
        : _MV{}
    {}
    MValue(
        MinionError& e)
        : _MV{std::make_shared<MError>(e)}
    {}
    MValue(
        MString& s)
        : _MV{std::make_shared<MString>(s)}
    {}
    MValue(
        std::string s)
        : _MV{std::make_shared<MString>(s)}
    {}
    MValue(
        const char* s)
        : _MV{std::make_shared<MString>(s)}
    {}
    MValue(
        MList& l)
        : _MV{std::make_shared<MList>(l)}
    {}
    MValue(
        MMap& m)
        : _MV{std::make_shared<MMap>(m)}
    {}
    // Using initializer_lists for list
    MValue(std::initializer_list<MValue> items);
    // A version for a map seems unlikely because of ambiguity (would
    // an element be a map pair or a two-element list?).

    int type() { return this->index(); }
    bool is_null() { return this->index() == 0; }

    std::shared_ptr<MString>* m_string() { return std::get_if<std::shared_ptr<MString>>(this); }
    std::shared_ptr<MList>* m_list() { return std::get_if<std::shared_ptr<MList>>(this); }
    std::shared_ptr<MMap>* m_map() { return std::get_if<std::shared_ptr<MMap>>(this); }
    const char* error_message()
    {
        auto m = std::get_if<std::shared_ptr<MError>>(this);
        if (m)
            return (*m)->message.c_str();
        return nullptr;
    }
};

class MString : public std::string
{};

class MList : public std::vector<MValue>
{
public:
    MValue& get(
        size_t index)
    {
        return this->at(index);
    }
    bool get_string(size_t index, std::string& s);
    bool get_int(size_t index, int& i);
};

using MPair = std::pair<std::string, MValue>;

class MMap : public std::vector<MPair>
{
    friend Reader;

public:
    MValue get(std::string_view key);
    MPair& get_pair(
        size_t index)
    {
        return this->at(index);
    }
    bool get_string(std::string_view key, std::string& s);
    bool get_int(std::string_view key, int& i);
};

// Used for recording read-position in input text
struct position
{
    size_t line_n;
    size_t byte_ix;
};

class Reader
{
    MMap macro_map;
    MValue get_macro(std::string_view s);

    std::string_view input;
    size_t ch_index;
    size_t line_index;
    size_t ch_linestart;
    std::string ch_buffer; // for reading strings

    std::string error_message;

    char read_ch(bool instring);
    void unread_ch();
    position here() { return {line_index + 1, ch_index - ch_linestart}; }
    std::string pos(
        position p)
    {
        return std::to_string(p.line_n) + '.' + std::to_string(p.byte_ix);
    }
    void error(std::string_view msg);

    MValue get_list();
    MValue get_map();

    void get_string();
    void get_bare_string(char ch);
    bool add_unicode_to_ch_buffer(int len);

    int get_token();
    std::string token_text(int token);

    Reader(std::string_view s);
    MValue result;

public:
    static MValue read(std::string_view s);
};

class Writer
{
    int indent = 2;
    int depth;
    std::string buffer;

    void add(
        char ch)
    {
        buffer.push_back(ch);
    }
    void pop() { buffer.pop_back(); }
    void dump_value(MValue& source);
    void dump_string(std::string_view source);
    void dump_list(MList& source);
    void dump_map(MMap& source);
    void dump_pad();

    Writer()
        : depth{0}
    {}

public:
    Writer(MValue& data, int pretty = -1);
    const char* dump_c();
    std::string_view dump();

    static std::string dumpString(std::string_view source);
};

} // namespace minion

#endif // MINION_H
