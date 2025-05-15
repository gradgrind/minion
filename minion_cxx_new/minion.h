#ifndef MINION_H
#define MINION_H

#include <initializer_list>
#include <map>
#include <stdexcept>
#include <vector>

//TODO: Adapt to new Minion class ...

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

/*
class MinionValue
{
    friend class Minion; // TODO?

    short type;
    short flags;
    int size;
    void* data;

    // constructor for explicit initialization
    MinionValue(short vtype, short vflags, int vsize, void* vdata);

public:
    MinionValue(); // null value constructor
    MinionValue(const char* text, bool simple = false); // string constructor
    // list/map constructor:
    MinionValue(MinionValue* items, int size, bool map = false);
    ~MinionValue();
    bool is_string();
    bool is_list();
    bool is_map();
    //TODO-- char* error_message();
    MinionValue find(const char* key); // only applicable to maps
};

struct map_item {
    MinionValue key;
    MinionValue value;
//TODO?
//    minion_map_item(char* k, MinionValue v) : key{k}, value{v} {}
};
*/

// Used for recording read-position in input text
struct position
{
    int line_n;
    int byte_ix;
};

// MinionValue is a fairly opaque class. Its actual data is managed by
// a Minion instance, and is accessible using methods of that instance.
class MinionValue
{
    friend class Minion; // TODO?

    short type;
    short flags;
    int data_index;

    // null value constructor
    MinionValue();
    // constructor for explicit initialization
    MinionValue(short type, short flags, int data);

public:
    bool is_string();
    bool is_list();
    bool is_map();

    //TODO?
    bool is_error();
};

struct map_pair
{
    std::string* key;
    MinionValue value;
};

union minion_data {
    std::string* s;
    std::vector<MinionValue>* l;
    std::vector<map_pair>* m;
};

class Minion
{
    // All memory allocations for the MinionValue items are stacked here:
    std::vector<minion_data> data;

    //TODO: Move to string& or string_view?
    // For character-by-character reading. The referenced memory is
    // not controlled by Minion.
    std::string_view input_string;
    int ch_index;
    int ch_linestart;
    int line_index;

    // read_buffer is used for constructinging strings when parsing text
    // input, before they are placed on the data stack.
    std::string read_buffer;

    // dump_buffer is used for serializing a minion_value.
    std::string dump_buffer;
    int indent = 2; // pretty-print indentation

    // Keep track of "unbound" minion items
    // The buffer for these items is used as a stack.
    std::vector<MinionValue> remembered_items;

    // Manage the macros
    std::map<std::string, MinionValue> macros;

    void error(std::string_view msg);

    int get_item();
    int get_string();
    int get_list();
    int get_map();

    char read_ch(bool instring);
    void unread_ch();
    position here();
    std::string pos(position p);
    bool add_unicode_to_read_buffer(int len);

public:
    MinionValue read(std::string_view input);

    /*?
    void clear_dump_buffer();
    void dump_ch(char ch);
    void undump_ch();
    MinionValue* find_macro(char* name);
    void remember(MinionValue minion_item);
    MinionValue last_item();
    bool is_key_unique(int i_start);
    void dump_string(const char* source);
    void dump_pad(int n);
    bool dump_list(MinionValue source, int depth);
    bool dump_map(MinionValue source, int depth);
    bool dump_value(MinionValue source, int depth);
    MinionValue pop_remembered();

public:

    ~Minion();

    MinionValue read(const char* input);
    
    MinionValue new_array(std::initializer_list<MinionValue> items);
    MinionValue new_map(std::initializer_list<map_item> items);

    //TODO: Change to use "pretty", which is the tab size. If 0 or less
    // the compact form will be used.
    char* dump(MinionValue source, int pretty);
    //char* dump(minion_value source, int depth);
 
    //TODO?
    void tidy_dump();
    void release();
    */
};

} // End namespace minion

/* TODO--
// The result must be freed when it is no longer needed
MinionValue minion_read(const char* input);

// Return the error message, or NULL if the item is not an error item
char* minion_error(MinionValue item);

// Free the memory used for a minion_value.
void minion_free(MinionValue item);

// Serialize a minion_value
//TODO: Perhaps I should have just a boolean argument for "pretty"/"compact"?
// ... or a value for the indentation steps? The depth value would surely be
// only for internal use.

// I could encapsulate this in a class so that it is more C++ like (with
// destructor):
char* minion_dump(MinionValue source, int depth);
// Free the memory used for a minion dump.
void minion_tidy_dump();

// Free longer term minion memory (can be retained between minion_read calls)
void minion_tidy();
//TODO: make this the destructor in Minion?

// Construction functions – these are designed to take on ownership of any
// minion_value items they are passed and need freeing with minion_free_item()
// when they are no longer in use.

MinionValue new_minion_string(const char* text);

//TODO ... how? MinionValue constructor? ... needs Minion item access ...
MinionValue new_minion_array(std::initializer_list<MinionValue> items);

struct pair_input // needed only for construction of maps in new_minion_map
{
    const char* key;
    MinionValue value;
};

//TODO ... how? MinionValue constructor? ... needs Minion item access ...
MinionValue new_minion_map(std::initializer_list<pair_input> items);

*/

#endif // MINION_H
