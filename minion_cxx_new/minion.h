#ifndef MINION_H
#define MINION_H

#include <initializer_list>
#include <stdexcept>

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

// Used for recording read-position in input text
struct position
{
    int line_n;
    int byte_ix;
};

// Node for building the macro map as a linked list
struct macro_node
{
    char* name;
    struct macro_node* next;
    MinionValue value;
};

class Minion
{
    // For character-by-character reading. These point to memory which is
    // not controlled by Minion, so they do not need freeing.
    const char* ch_pointer0;
    const char* ch_pointer;
    const char* ch_linestart = 0;
    int line_index;

    // read_buffer is used for constructinging strings before they are
    // passed to minion_value items.
    char* read_buffer = 0;
    size_t read_buffer_size = 0;
    size_t read_buffer_index = 0;

    // dump_buffer is used for serializing a minion_value.
    char* dump_buffer = 0;
    int dump_buffer_size = 0;
    int dump_buffer_index = 0;
    int indent = 2; // pretty-print indentation

    /* TODO-- Error handling ... use exceptions
    jmp_buf recover; // for error recovery

    // error_message is a buffer used in reporting errors. The error message
    // is stored here before being passed to a special minion_value (type
    // T_Error).
    char* error_message = 0;
    int error_message_size = 0;
    */

    //TODO-- char position_buffer[position_size];

    // Keep track of "unbound" minion items
    // The buffer for these items is used as a stack.
    MinionValue* remembered_items = 0;
    int remembered_items_size = 0;
    int remembered_items_index = 0;

    // Manage the macros
    macro_node* macros = NULL;

    void reset_read_buffer_index();
    void add_to_read_buffer(char ch);
    void clear_dump_buffer();
    void dump_ch(char ch);
    void undump_ch();
    void error(std::string_view msg);
    MinionValue* find_macro(char* name);
    void remember(MinionValue minion_item);
    position here();
    std::string pos(position p);
    char read_ch(bool instring);
    void unread_ch();
    bool add_unicode_to_read_buffer(int len);
    int get_string();
    int get_list();
    MinionValue last_item();
    bool is_key_unique(int i_start);
    int get_map();
    int get_item();
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
