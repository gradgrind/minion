#ifndef MINION_H
#define MINION_H

#include <initializer_list>

typedef struct
{
    short type;
    short flags;
    unsigned int size;
    void* data;
} minion_value;

typedef struct macro_node
{
    char* name;
    struct macro_node* next;
    minion_value value;
} macro_node;

typedef struct
{
    minion_value minion_item;
    minion_value error;
    macro_node* macros;
} minion_doc;

// The result must be freed when it is no longer needed
minion_doc minion_read(const char* input);

// Return the error message, or NULL if there was no error
char* minion_error(minion_doc doc);

// Free the memory used for a minion_doc.
void minion_free(minion_doc doc);

char* minion_dump(minion_value source, int depth);
// Free the memory used for a minion dump.
void minion_tidy_dump();

// Free longer term minion memory (can be retained between minion_read calls)
void minion_tidy();

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

void minion_free_item(minion_value mitem);

#endif // MINION_H
