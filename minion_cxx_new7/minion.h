#ifndef MINION_H
#define MINION_H

#include <forward_list>
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

// *** forward declarations ***
struct MValue;
struct MinionValue;
class InputBuffer;
class DumpBuffer;
using MPair = std::pair<std::string, MValue>;
class MString;
class MList;
class MMap;

struct MValue
{
    friend MinionValue;
    friend InputBuffer;
    friend DumpBuffer;

    MValue() = default;
    MValue(std::string_view s);
    MValue(std::initializer_list<MValue> items);
    MValue(std::initializer_list<MPair> items);

    MString* m_string();
    MList* m_list();
    MMap* m_map();

    void copy(MinionValue& m); // deep copy function

    //protected:
    void free();

    //TODO++ protected:
    int type{0};
    bool not_owner{false};
    void* minion_item{nullptr};

    MValue(
        int t, void* p, bool o = false)
        : type{t}
        , not_owner{o}
        , minion_item{p}
    {}

    MValue unowned() { return MValue(type, minion_item, true); }

    void mcopy(MValue& m); // used by copy method
};

//TODO--
extern MValue* base_ref;
//--

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
};

class MString : public std::string
{};

class MList : public std::vector<MValue>
{};

class MMap : public std::vector<MPair*>
{};

class InputBuffer
{
    //TODO--
public:
    //--

    class MacroMap
    {
        //TODO--
    public:
        //--
        std::forward_list<MPair> macros;

    public:
        void clear()
        {
            for (auto& mp : macros) {
                mp.second.free();
            }
            macros.clear();
        }

        bool has(
            std::string& key)
        {
            for (auto& mp : macros) {
                if (mp.first == key) {
                    return true;
                }
            }
            return false;
        }

        MValue& first_value() { return macros.front().second; }

        MValue get(
            std::string& key)
        {
            for (auto& mp : macros) {
                if (mp.first == key) {
                    auto m = mp.second;
                    mp.second.not_owner = true;
                    return m;
                }
            }
            return {};
        }

        MPair& add(
            std::string& key, MValue value)
        {
            macros.emplace_front(key, value);
            return macros.front();
        }
    };

    MValue get_macro(std::string& s);

    //MinionValue data;

    std::string_view input;
    size_t ch_index;
    size_t line_index;
    size_t ch_linestart;
    std::string ch_buffer; // for reading strings

    MacroMap macro_map;

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
    //void get_map_value(MValue& mvalue);

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
    void dump_value(const MValue& source);
    void dump_string(const std::string& source);
    void dump_list(MList& source);
    void dump_map(MMap& source);
    void dump_pad();

public:
    const char* dump(MValue& data, int pretty = -1);
};

} // namespace minion

#endif // MINION_H
