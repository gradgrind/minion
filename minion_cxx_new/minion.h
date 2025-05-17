#ifndef MINION_H
#define MINION_H

#include <initializer_list>
#include <map>
#include <stdexcept>
#include <variant>
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

// Used for recording read-position in input text
struct position
{
    int line_n;
    int byte_ix;
};

class MinionList;
class MinionMap;
using MinionValue = std::variant<std::monostate, std::string, MinionList, MinionMap>;

struct map_pair
{
    std::string* key; //TODO: string_view?
    MinionValue* value;
};

class MinionList : public std::vector<MinionValue*>
{};

class MinionMap : public std::vector<map_pair>
{};

class Minion
{
    // The addresses of all heap-allocated MinionValue items are stacked
    // here as the "owning" pointers, so that deletion can be centrally
    // managed (see free_data()). All MinionValue pointers in the data
    // structures are then "weak" pointers, whose validity depends on the
    // management of this stack.
    std::vector<MinionValue*> data;

    // For character-by-character reading of MINION input.
    // The referenced memory is not controlled by Minion.
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

    // A stack-like buffer for building MinionValue items.
    std::vector<MinionValue*> remembered_items;

    // Manage the macros
    std::map<std::string_view, MinionValue*> macros;

    void error(std::string_view msg);

    int get_item();
    void get_string();
    void get_list();
    void get_map();

    char read_ch(bool instring);
    void unread_ch();
    position here();
    std::string pos(position p);
    bool add_unicode_to_read_buffer(int len);

public:
    MinionValue* read(std::string_view input);
    void free_data();

    /*?
    void clear_dump_buffer();
    void dump_ch(char ch);
    void undump_ch();
    MinionValue* find_macro(char* name);
    void remember(MinionValue minion_item);
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
