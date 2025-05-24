#ifndef MINION_H
#define MINION_H

#include <stdexcept>
#include <vector>

/* The parser, Minion::read returns a single minion_value. If there is an
 * error, a MinionError exception will be thrown.
 */
 
namespace minion {

class MinionError : public std::runtime_error {
public:
	// Using constructor for passing custom message
	MinionError(const std::string& message)
        : runtime_error(message) {}
};

// Used for recording read-position in input text
struct position
{
    size_t line_n;
    size_t byte_ix;
};

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
    void get_item(MValue& mvalue, int expect = 0);
    void get_string(char ch);
    void get_bare_string(char ch);
    bool add_unicode_to_ch_buffer(int len);

public:
    const char* read(MinionValue& data, std::string_view s);
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
    void dump_string(MString& source);
    void dump_list(MList& source);
    void dump_map(MMap& source);
    void dump_pad();

public:
    const char* dump(MValue& data, int pretty = -1);
};

} // namespace minion

#endif // MINION_H
