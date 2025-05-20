#ifndef MINION_H
#define MINION_H

#include <map>
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

//class DumpBuffer; // forward declaration

struct MValue
{
    int type;
    void* minion_item;

    MValue copy(); // deep copy function
};

using MString = std::string;
using MList = std::vector<MValue>;

struct MPair
{
    MString key;
    MValue value;
};

using MMap = std::vector<MPair>;

void delete_mvalue(MValue& m);

class MacroMap
{
    std::map<std::string, MValue> macros;

public:
    void clear()
    {
        for (auto& mp : macros) {
            delete_mvalue(mp.second);
        }
        macros.clear();
    }

    bool has(
        std::string& key)
    {
        return macros.contains(key);
    }

    MValue get(
        std::string& key)
    {
        return macros.at(key);
    }

    void add(
        std::string& key, MValue value)
    {
        macros.emplace(key, value);
    }
};

class InputBuffer
{
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
    int get_item(MValue& value_buffer);
    void get_string();
    bool add_unicode_to_ch_buffer(int len);
    void get_list(MValue& value_buffer);
    void get_map(MValue& value_buffer);

public:
    MValue read(std::string& s);
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
