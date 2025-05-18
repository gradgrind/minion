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

class DumpBuffer; // forward declaration

class MValue
{
public:
    virtual ~MValue() {};
    virtual int type() { return 0; }
};

class MString : public MValue
{
    friend DumpBuffer;
    std::string data;

public:
    MString(
        const std::string_view s)
        : MValue()
    {
        data = s;
    }
    int type() override { return 1; }
};

class MList : public MValue
{
    friend DumpBuffer;
    std::vector<MValue> data;

public:
    int type() override { return 2; }
    void add(
        MValue m)
    {
        data.emplace_back(std::move(m));
    }
    bool dump(DumpBuffer& buffer);
};

struct MPair
{
    std::string key;
    MValue value;
};

class MMap : public MValue
{
    friend DumpBuffer;
    std::vector<MPair> data;

public:
    int type() override { return 3; }
    void add(
        MPair m)
    {
        data.emplace_back(std::move(m));
    }
    bool get(
        std::string& key, MValue& value)
    {
        for (const auto& kv : data) {
            if (kv.key == key) {
                value = kv.value;
                return true;
            }
        }
        return false;
    }
};

class InputBuffer
{
    MValue data;
    std::string_view input;
    size_t ch_index;
    size_t line_index;
    size_t ch_linestart;
    std::string ch_buffer; // for reading strings
    std::map<std::string, MValue> macros;

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
    MList get_list();
    MMap get_map();

public:
    MValue* read(std::string& s);
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
    void dump_string(std::string& source);
    void dump_list(MList& source);
    void dump_map(MMap& source);
    void dump_pad();

public:
    const char* dump(MValue& data, int pretty = -1);
};

} // namespace minion

#endif // MINION_H
