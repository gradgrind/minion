#ifndef MINION_H
#define MINION_H
#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int msize;

typedef struct
{
    short type;
    short flags;
    msize size;
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

minion_doc minion_read(const char* input);
char* minion_error(minion_doc doc);
// Free the memory used for a minion item.
void minion_free(minion_doc doc);

char* minion_dump(minion_value source, int depth);
// Free the memory used for a minion dump.
void minion_tidy_dump();

// Free longer term minion memory (can be retained between minion_read calls)
void minion_tidy();

#ifdef __cplusplus
}
#endif
#endif // MINION_H
