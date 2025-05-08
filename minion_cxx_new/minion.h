#ifndef MINION_H
#define MINION_H

#include <initializer_list>

//TODO: Adapt to new Minion class ...

/* THe parser, Minion::read returns a single minion_value. If there is an
 * error, a special minion_value will be returned, which can be checked
 * using minion_error().
 */
 
/* Macros are handled without needing to copy their data. Immediately
 * after definition they are stored as normal minion_values in some sort of
 * map so that they can be accessed by name. WHen a reference to a macro is
 * found, it will be copied, but not the memory pointed to by the data
 * field (i.e. it is a shallow copy). As the structure is read-only, this
 * should be no problem, the data fields are just shared.
 * However, care must be taken when freeing the structure – that the memory
 * is not freed twice. To handle this there is a special "not owner" flag.
 * After the first copy of the macro value, this field is set (in the value
 + in the macro map) so that subsequent copies are marked as "not owner".
 * Thus ownership is transferred to the first reference.
 * Should a macro never be referenced, the value in the mcro map will still
 * be the owner at the end of the parse. Such cases should then be freed
 * automatically. Perhaps this should count as a parsing error?
*/

//TODO: This needs a destructor to free the memory (recursively).
//TODO: This needs a find() method to do key lookup in a map (fail if not
// map ...)
struct minion_value
{
    short type;
    short flags;
    unsigned int size;
    void* data;
};

// The result must be freed when it is no longer needed
minion_value minion_read(const char* input);

// Return the error message, or NULL if the item is not an error item
char* minion_error(minion_value item);

// Free the memory used for a minion_value.
void minion_free(minion_value item);

// Serialize a minion_value
//TODO: Perhaps I should have just a boolean argument for "pretty"/"compact"?
// ... or a value for the indentation steps? The depth value would surely be
// only for internal use.

// I could encapsulate this in a class so that it is more C++ like (with
// destructor):
char* minion_dump(minion_value source, int depth);
// Free the memory used for a minion dump.
void minion_tidy_dump();

// Free longer term minion memory (can be retained between minion_read calls)
void minion_tidy();
//TODO: make this the destructor in Minion?

// Construction functions – these are designed to take on ownership of any
// minion_value items they are passed and need freeing with minion_free_item()
// when they are no longer in use.

minion_value new_minion_string(const char* text);

minion_value new_minion_array(std::initializer_list<minion_value> items);

struct pair_input // needed only for construction of maps in new_minion_map
{
    const char* key;
    minion_value value;
};

minion_value new_minion_map(std::initializer_list<pair_input> items);

#endif // MINION_H
