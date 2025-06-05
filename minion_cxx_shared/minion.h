#ifndef MINION_H
#define MINION_H

#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

/* The parser, InputBuffer::read returns a single MValue. If there is an
 * error, a MinionError exception will be thrown internally and caught in
 * InputBuffer::read, which then returns a special MValue with the error
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

class InputBuffer;

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
    friend InputBuffer;

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

class InputBuffer
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
    //TODO-- MValue get_item(int expect = 0);

    MValue get_list();
    MValue get_map();

    void get_string();
    void get_bare_string(char ch);
    bool add_unicode_to_ch_buffer(int len);

    int get_token();
    std::string token_text(int token);

public:
    MValue read(std::string_view s);
};

class DumpBuffer
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
    //void dump_string(MString& source);
    void dump_list(MList& source);
    void dump_map(MMap& source);
    void dump_pad();

public:
    const char* dump(MValue& data, int pretty = -1);
};

} // namespace minion

#endif // MINION_H

/*

// forward declarations
struct MValue;
using MPair = std::pair<std::string, MValue>;
struct MinionValue;
class InputBuffer;
class DumpBuffer;
class MString;
class MList;
class MMap;

struct MValue
{
    friend MinionValue;
    friend MList;
    friend MMap;
    friend InputBuffer;
    friend DumpBuffer;

    MValue() = default;
    MValue(MString* m);
    MValue(MList* m);
    MValue(MMap* m);

    bool is_null() { return type == 0; }

    MString* m_string();
    MList* m_list();
    MMap* m_map();

    void copy(MinionValue& m); // deep copy function

protected:
    void free();

    int type{0};
    bool not_owner{false};
    void* minion_item{nullptr};

    MValue(
        int t, void* p, bool o = false)
        : type{t}
        , not_owner{o}
        , minion_item{p}
    {}

    void mcopy(MValue& m); // used by copy method
};

struct MinionValue : public MValue
{
    MinionValue() = default;
    MinionValue(
        MValue m)
    {
        type = m.type;
        minion_item = m.minion_item;
    }

    ~MinionValue() { free(); }

    MinionValue& operator=(
        const MinionValue& source)
    {
        // self-assignment check
        if (this != &source) {
            this->free();
            type = source.type;
            minion_item = source.minion_item;
            not_owner = false;
        }
        return *this;
    }
};

class MString
{
    std::string data;

public:
    MString() = default;

    MString(
        std::string_view s)
        : data{s}
    {}

    ~MString() = default;

    std::string_view data_view() { return data; }
};

class MList
{
    std::vector<MValue> data;

public:
    MList() = default;

    MList(
        std::initializer_list<MValue> items)
    {
        for (const auto& item : items) {
            add(item);
        }
    }

    MList(
        MList& source) // copy constructor
    {
        data.reserve(source.data.size());
        for (auto& mv : source.data) {   // mv is reference to source element
            data.emplace_back(MValue{}); // add null MValue
            MValue& mref = data.back();  // get reference to added MValue
            mv.mcopy(mref);
        }
    }

    ~MList()
    {
        for (auto& m : data) {
            m.free();
        }
    }

    size_t size() { return data.size(); }

    void add(
        MValue m)
    {
        data.emplace_back(m);
    }

    MValue& get(
        size_t index)
    {
        return data.at(index);
    }

    bool get_string(size_t index, std::string& s);
    bool get_int(size_t index, int& i);
};

class MMap
{
    std::vector<MPair> data;

public:
    MMap() = default;

    MMap(
        std::initializer_list<MPair> items)
    {
        for (const auto& item : items) {
            add(item);
        }
    }

    MMap(
        MMap& source) // copy constructor
    {
        data.reserve(source.data.size());
        for (auto& mp : source.data) { // mv is reference to source element
            // add pair with null MValue
            data.emplace_back(MPair{mp.first, {}});
            MValue& mref = data.back().second; // get reference to added MValue
            mp.second.mcopy(mref);
        }
    }

    ~MMap() { clear(); }

    void clear()
    {
        for (auto& m : data) {
            m.second.free();
        }
        data.clear();
    }

    size_t size() { return data.size(); }

    void add(
        MPair m)
    {
        data.emplace_back(m);
    }

    MPair& get_pair(
        size_t index)
    {
        return data.at(index);
    }

    int search(
        std::string_view key)
    {
        int i = 0;
        for (auto& mp : data) {
            if (mp.first == key)
                return i;
            ++i;
        }
        return -1;
    }

    MValue get(
        std::string_view key)
    {
        for (auto& mp : data) {
            if (mp.first == key)
                return mp.second;
        }
        return {};
    }

    bool get_string(std::string_view key, std::string& s);
    bool get_int(std::string_view key, int& i);
};

*/
