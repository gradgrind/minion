#include "minion.h"
//#include <stdbool.h>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* All the values in minion_Flags (apart from the null F_NoFlags) are
 * greater than the highest value in minion_Type, so that when type is
 * T_NoType the flags value can be used instead without ambiguity.
*/
#define MIN_FLAG 8
typedef enum {
    F_NoFlags = 0,
    F_Simple_String = MIN_FLAG, // undelimited string
    F_Error,
    F_Macro,
    F_Token_End,
    F_Token_String_Delim,
    F_Token_ListStart,
    F_Token_ListEnd,
    F_Token_MapStart,
    F_Token_MapEnd,
    F_Token_Comma,
    F_Token_Colon,

    // This bit will be set if the data field refers to memory that this
    // item does not "own", i.e. it shouldn't be freed.
    F_MACRO_VALUE = 32
} minion_Flags;

typedef enum {
    T_NoType = 0,
    T_String,
    T_Array,
    T_PairArray,
} minion_Type;

// For character-by-character reading
static const char* ch_pointer0;
static const char* ch_pointer;
static const char* ch_linestart = 0;
static msize line_index;

/* *** BUFFERS ***
 * minion uses buffers for various purposes. These get their space using
 * malloc and may grow when more space is needed.
 * This space is not freed, so that once a buffer has been created it can
 * be reused. Also at the end of a call to minion_read it is not
 * necessary to free the space, so that subsequent calls may not need to
 * reallocate the space. However, it may be desirable to free this space
 * at some time, so for this purpose there is the function minion_tidy().
*/

// read_buffer is used for constructinging strings before they are passed
// to minion_value items.
static char* read_buffer = 0;
#define read_buffer_size_increment 100
static size_t read_buffer_size = 0;
static size_t read_buffer_index = 0;
void reset_read_buffer_index()
{
    read_buffer_index = 0;
}

void add_to_read_buffer(
    char ch)
{
    if (read_buffer_index == read_buffer_size) {
        // Increase the size of the buffer
        void* tmp = malloc(read_buffer_size + read_buffer_size_increment);
        if (!tmp)
            exit(1);
        memcpy(tmp, read_buffer, read_buffer_size);
        free(read_buffer);
        read_buffer = (char*) tmp;
        read_buffer_size += read_buffer_size_increment;
    }
    read_buffer[read_buffer_index++] = ch;
}

// dump_buffer is used for serializing a minion_value.
static char* dump_buffer = 0;
#define dump_buffer_size_increment 1000
static int dump_buffer_size = 0;
static int dump_buffer_index = 0;

void clear_dump_buffer()
{
    dump_buffer_index = 0;
}

void dump_ch(
    char ch)
{
    if (dump_buffer_index == dump_buffer_size) {
        // Increase the size of the buffer
        void* tmp = malloc(dump_buffer_size + dump_buffer_size_increment);
        if (!tmp)
            exit(1);
        memcpy(tmp, dump_buffer, dump_buffer_size);
        free(dump_buffer);
        dump_buffer = (char*) tmp;
        dump_buffer_size += dump_buffer_size_increment;
    }
    dump_buffer[dump_buffer_index++] = ch;
}

// Remove last added character – useful for trailing commas.
void undump_ch()
{
    dump_buffer_index--;
}

// Error handling
jmp_buf recover; // for error recovery

// error_message is a buffer used in reporting errors. The error message
// is stored here before being passed to a special minion_value (type
// T_Error).
static char* error_message = 0;
static int error_message_size = 0;

void error(
    const char* msg, ...)
{
    va_list args;
    va_start(args, msg);
    // Get string size (add 1 for 0-terminator) without writing anything
    int n = vsnprintf(0, 0, msg, args);
    //int n = vsnprintf(error_message, error_message_size, msg, args);
    if (n < 0) {
        fprintf(stderr, "[BUG] Invalid error message: %s\n", msg);
        exit(100);
    }

    // Add most recently read characters
    const char* ch_start = ch_pointer0;
    int recent = ch_pointer - ch_start;
    if (recent > 80) {
        ch_start = ch_pointer - 80;
        // Find start of utf-8 sequence
        while (true) {
            unsigned char ch = *ch_start;
            if (ch < 0x80 || (ch >= 0xC0 && ch < 0xF8))
                break;
            ++ch_start;
        }
        recent = ch_pointer - ch_start;
    }
    int nx = n + recent + 10;
    if (error_message_size < nx) {
        free(error_message);
        error_message = (char*) malloc(nx);
        if (!error_message)
            exit(1);
        error_message_size = nx;
    }

    /*
    if (error_message_size < ++n) {
        free(error_message);
        error_message = malloc(n);
        if (!error_message)
            exit(1);
        error_message_size = n;
    }
    */

    va_start(args, msg); // restart the argument reading
    vsnprintf(error_message, error_message_size, msg, args);
    va_end(args);

    n += snprintf(error_message + n, 8, "\n ... ");
    memcpy(error_message + n, ch_start, recent);
    error_message[n + recent] = 0;

    longjmp(recover, 2);
}

// Keep track of "unbound" minion items
// The buffer for these items is used as a stack.
static minion_value* remembered_items = 0;
#define remembered_items_size_increment 10
static int remembered_items_size = 0;
static int remembered_items_index = 0;

typedef struct
{
    minion_value key;
    minion_value value;
} minion_pair;

// Free the memory used for a minion item.
void free_item(
    minion_value mitem)
{
    if (mitem.flags & F_MACRO_VALUE)
        return;
    if (mitem.type == T_Array) {
        minion_value* p = (minion_value*) mitem.data;
        msize n = mitem.size;
        for (msize i = 0; i < n; ++i) {
            free_item(p[i]);
        }
    } else if (mitem.type == T_PairArray) {
        minion_pair* p = (minion_pair*) mitem.data;
        msize n = mitem.size;
        for (msize i = 0; i < n; ++i) {
            minion_pair mp = p[i];
            free_item(mp.key);
            free_item(mp.value);
        }
    }
    // Free the memory pointed to directly by the data field. This will
    // collect the actual array storage from the above items or the
    // character storage for strings, etc.
    free(mitem.data);
    // free() does nothing if the address it gets is NULL. But if
    // non-pointer types should be used sometime with values in the
    // data field, a further type/flag test would be needed to avoid
    // trying to free these!
}

// Manage the macros
static macro_node* macros = NULL;

void new_macro() {}

void free_macros(
    macro_node* mp)
{
    while (mp) {
        free(mp->name);
        free_item(mp->value);
        macro_node* mp0 = mp;
        mp = mp->next;
        free(mp0);
    }
}

minion_value* find_macro(
    char* name)
{
    macro_node* mp = macros;
    while (mp) {
        if (strcmp(name, mp->name) == 0) {
            return &mp->value;
        }
        mp = mp->next;
    }
    return NULL;
}

bool real_minion_value(
    short mtype)
{
    return (mtype != T_NoType && mtype < MIN_FLAG);
}

/* Some allocated memory can be retained between minion_read calls,
 * but it should probably be freed sometime, if it is really no
 * longer needed.
*/
// The dump memory can be freed on its own.
void minion_tidy_dump()
{
    free(dump_buffer);
    dump_buffer = 0;
    dump_buffer_size = 0;
    dump_buffer_index = 0;
}

// Free all longer-term buffers.
void minion_tidy()
{
    free(read_buffer);
    //printf("??? Size of read_buffer = %zu\n", read_buffer_size);
    read_buffer = 0;
    read_buffer_size = 0;
    read_buffer_index = 0;

    free(error_message);
    error_message = 0;
    error_message_size = 0;

    free(remembered_items);
    remembered_items = 0;
    remembered_items_size = 0;
    remembered_items_index = 0;
}

bool minion_isString(
    minion_value v)
{
    return (v.type == T_String);
}

void minion_free(
    minion_doc doc)
{
    free_item(doc.minion_item);
    free_macros(doc.macros);
    free_item(doc.error);
}

void remember(
    minion_value minion_item)
{
    if (remembered_items_index == remembered_items_size) {
        // Need more space
        int n = remembered_items_size + remembered_items_size_increment;
        minion_value* tmp = (minion_value*) realloc(remembered_items, n * sizeof(minion_value));
        if (tmp == NULL) {
            // alloc failed
            free(remembered_items);
            exit(1);
        } else if (tmp != remembered_items)
            remembered_items = tmp;
        remembered_items_size = n;
    }
    // Add new item
    remembered_items[remembered_items_index++] = minion_item;
}

void release()
{
    for (int i = 0; i < remembered_items_index; ++i) {
        free_item(remembered_items[i]);
    }
    remembered_items_index = 0;
}

// --- END: Keep track of "unbound" malloced items ---

// Build a new String item from a char*. Place the result on the
// remember stack.
void new_String(
    const char* text, minion_Type stype, minion_Flags sflags)
{
    // Remember "+1" for 0-terminator
    unsigned int l = strlen(text);
    void* s = malloc(sizeof(char) * (l + 1));
    if (!s)
        exit(1);
    memcpy(s, text, l + 1);
    remember((minion_value) {(short)stype, (short) sflags, l, s});
}

// Build a new Array item from items on the stack, the starting index
// being passed as argument.
// Place the result on the remember stack.
void new_Array(
    int start_index)
{
    void* a = 0;
    int len = remembered_items_index - start_index;
    if (len > 0) {
        size_t nbytes = sizeof(minion_value) * len;
        a = malloc(nbytes);
        if (!a)
            exit(1);
        memcpy(a, &remembered_items[start_index], nbytes);
        // Remove the component items before the Array item is addded.
        remembered_items_index = start_index;
    } else if (len < 0) {
        fputs("[BUG] In new_Array: remembered_items_index < start_index", stderr);
        exit(100);
    }
    remember((minion_value) {T_Array, 0, (msize) len, a});
}

// Build a new PairArray item from items on the stack, the starting index
// being passed as argument.
// Place the result on the remember stack.
void new_PairArray(
    int start_index)
{
    void* a = 0;
    int len = remembered_items_index - start_index;
    if (len & 1) {
        // Each entry is a pair, i.e. it consists of two items.
        fputs("[BUG] In new_PairArray: odd number of items on stack", stderr);
        exit(100);
    }
    if (len > 0) {
        len /= 2; // A key/value pair makes up one Pair item
        size_t nbytes = sizeof(minion_pair) * len;
        a = malloc(nbytes);
        if (!a)
            exit(1);
        memcpy(a, &remembered_items[start_index], nbytes);
        // Remove the component items before the PairArray item is addded.
        remembered_items_index = start_index;
    } else if (len < 0) {
        fputs("[BUG] In new_PairArray: remembered_items_index < start_index", stderr);
        exit(100);
    }
    remember((minion_value) {T_PairArray, 0, (msize) len, a});
}

typedef struct
{
    msize line_n;
    msize byte_ix;
} position;

position here()
{
    return (position) {line_index + 1, (msize) (ch_pointer - ch_linestart)};
}

#define position_size 20
static char position_buffer[position_size];
char* pos(
    position p)
{
    snprintf(position_buffer, position_size, "%d.%d", p.line_n, p.byte_ix);
    return position_buffer;
}

static char read_ch(
    bool instring)
{
    char ch = *ch_pointer;
    if (ch == 0) {
        // end of data
        return 0;
    }
    ++ch_pointer;
    if (ch == '\n') {
        ++line_index;
        ch_linestart = ch_pointer;
        // these are not acceptable within delimited strings
        if (!instring) {
            // separator
            return ch;
        }
        error("Unexpected newline in delimited string, line %d", line_index);
        exit(3); // unreachable
    } else if (ch == '\r' || ch == '\t') {
        // these are acceptable in the source, but not within strings.
        if (!instring) {
            // separator
            return ' ';
        }
    } else if ((unsigned char) ch >= 32 && ch != 127) {
        return ch;
    }
    error("Illegal character (%2x) at position %s", (unsigned char) ch, pos(here()));
    exit(3); // unreachable
}

void unread_ch()
{
    if (ch_pointer == ch_pointer0) {
        fputs("[BUG] unread_ch reached start of data", stderr);
        exit(100);
    }
    --ch_pointer;
    //NOTE: '\n' is never unread!
}
// --- END: Handle character-by-character reading ---

// Convert a unicode code point (as hex string) to a UTF-8 string
bool add_unicode_to_read_buffer(
    int len)
{
    // Convert the unicode to an integer
    char ch;
    int digit;
    unsigned int code_point = 0;
    for (int i = 0; i < len; ++i) {
        ch = read_ch(true);
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        } else
            return false;
        code_point *= 16;
        code_point += digit;
    }
    // Convert the code point to a UTF-8 string
    if (code_point <= 0x7F) {
        add_to_read_buffer(code_point);
    } else if (code_point <= 0x7FF) {
        add_to_read_buffer((code_point >> 6) | 0xC0);
        add_to_read_buffer((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0xFFFF) {
        add_to_read_buffer((code_point >> 12) | 0xE0);
        add_to_read_buffer(((code_point >> 6) & 0x3F) | 0x80);
        add_to_read_buffer((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0x10FFFF) {
        add_to_read_buffer((code_point >> 18) | 0xF0);
        add_to_read_buffer(((code_point >> 12) & 0x3F) | 0x80);
        add_to_read_buffer(((code_point >> 6) & 0x3F) | 0x80);
        add_to_read_buffer((code_point & 0x3F) | 0x80);
    } else {
        // Invalid input
        return false;
    }
    return true;
}

// The "get_" family of functions reads the corresponding item from the
// input. If the result is a minion item, that will be placed on the
// remember stack. The "get_" functions return the type of the item that
// was read – the structural tokens have no malloced memory, so they do
// not need to be stacked.

/* Read a delimited string (terminated by '"') from the input.
 *
 * It is entered after the initial '"' has been read, so the next character
 * will be the first of the string.
 *
 * Escapes, introduced by '\', are possible. These are an extension of the
 * JSON escapes – see the MINION specification.
 *
 * Return the string as a minion_value.
 */
minion_Type get_string()
{
    position start_pos = here();
    char ch;
    while (true) {
        ch = read_ch(true);
        if (ch == '"')
            break;
        if (ch == 0) {
            error("End of data reached inside delimited string from position %s", pos(start_pos));
            exit(3); // unreachable
        }
        if (ch == '\\') {
            ch = read_ch(false); // '\n' etc. are permitted here
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'n':
                ch = '\n';
                break;
            case 't':
                ch = '\t';
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'r':
                ch = '\r';
                break;
            case 'u':
                if (add_unicode_to_read_buffer(4))
                    continue;
                error("Invalid unicode escape in string, position %s", pos(here()));
                exit(3); // unreachable
            case 'U':
                if (add_unicode_to_read_buffer(6))
                    continue;
                error("Invalid unicode escape in string, position %s", pos(here()));
                exit(3); // unreachable
            case '[':
                // embedded comment, read to "\]"
                {
                    position comment_pos = here();
                    ch = read_ch(false);
                    while (true) {
                        if (ch == '\\') {
                            ch = read_ch(false);
                            if (ch == ']') {
                                break;
                            }
                            continue;
                        }
                        if (ch == 0) {
                            error("End of data reached inside string comment from position %s",
                                  pos(comment_pos));
                            exit(3); // unreachable
                        }
                        // loop with next character
                        ch = read_ch(false);
                    }
                }
                continue; // comment ended, seek next character
            default:
                error("Illegal string escape at position %s", pos(here()));
                exit(3); // unreachable
            }
        }
        add_to_read_buffer(ch);
    }
    // Add 0-terminator
    add_to_read_buffer(0);
    new_String(read_buffer, T_String, F_NoFlags);
    return T_String;
}

short get_item();

minion_Type get_list()
{
    int start_index = remembered_items_index;
    position current_pos = here();
    short mtype = get_item();
    while (true) {
        // ',' before the closing bracket is allowed
        if (mtype == F_Token_ListEnd)
            break;
        if (real_minion_value(mtype)) {
            current_pos = here();
            mtype = get_item();
            if (mtype == F_Token_ListEnd) {
                break;
            } else if (mtype == F_Token_Comma) {
                current_pos = here();
                mtype = get_item();
                continue;
            }
            error("Reading list, expecting ',' or ']' at position %s", pos(current_pos));
            exit(3); // unreachable
        }
        if (mtype == F_Macro) {
            error("Undefined macro name at position %s", pos(current_pos));
            exit(3); // unreachable
        } else {
            error("Expecting list item or ']' at position %s", pos(current_pos));
            exit(3); // unreachable
        }
    }
    new_Array(start_index);
    return T_Array;
}

minion_value last_item()
{
    if (remembered_items_index) {
        return remembered_items[remembered_items_index - 1];
    }
    fputs("[BUG] 'last_item: remembered' stack corrupted", stderr);
    exit(100);
}

bool is_key_unique(
    int i_start)
{
    char* key = (char*) last_item().data;
    int i = i_start;
    int i_end = remembered_items_index - 1; // index of key
    while (i < i_end) {
        if (strcmp((char*) remembered_items[i].data, key) == 0)
            return false;
        i += 2;
    }
    return true;
}

minion_Type get_map()
{
    int start_index = remembered_items_index;
    position current_pos = here();
    short mtype = get_item();
    char const* seeking;
    while (true) {
        // ',' before the closing bracket is allowed
        if (mtype == F_Token_MapEnd)
            break;
        // expect key
        if (mtype == T_String) {
            if (!is_key_unique(start_index)) {
                error("Map key has already been defined: %s (at position %s)",
                      last_item().data,
                      pos(current_pos));
            }
            current_pos = here();
            mtype = get_item();
            // expect ':'
            if (mtype != F_Token_Colon) {
                error("Expecting ':' in Map item at position %s", pos(current_pos));
                exit(3); // unreachable
            }
            current_pos = here();
            mtype = get_item();
            // expect value
            seeking = "Reading map, expecting a value at position %s";
            if (real_minion_value(mtype)) {
                current_pos = here();
                mtype = get_item();
                if (mtype == F_Token_MapEnd) {
                    break;
                } else if (mtype == F_Token_Comma) {
                    current_pos = here();
                    mtype = get_item();
                    continue;
                }
                error("Reading map, expecting ',' or '}' at position %s", pos(current_pos));
                exit(3); // unreachable
            } else if (mtype == F_Macro) {
                seeking = "Expecting map value, undefined macro name at position %s";
            }
        } else {
            seeking = "Reading map, expecting a key at position %s";
        }
        error(seeking, pos(current_pos));
        exit(3); // unreachable
    }
    new_PairArray(start_index);
    return T_PairArray;
}

/* Read the next "item" from the input.
 * Return the minion_Type of the item read, which may be a string, an
 * "array" (list) or an "object" (map). If the input is invalid, a
 * special "error" value will be returned, containing a message.
 * Also the structural symbols have types.
 * 
 * Strings are read into a buffer, which grows if it is too small.
 * Compound items are constructed by reading their components onto a stack.
*/
short get_item()
{
    char ch;
    reset_read_buffer_index();
    short result;
    while (true) {
        ch = read_ch(false);
        if (read_buffer_index != 0) {
            // An undelimited string item has already been started
            while (true) {
                // Test for an item-terminating character
                if (ch == ' ' || ch == '\n')
                    break;
                if (ch == ':' || ch == ',' || ch == ']' || ch == '}') {
                    unread_ch();
                    break;
                }
                if (ch == 0)
                    break;
                if (ch == '{' || ch == '[' || ch == '\\' || ch == '"') {
                    error("Unexpected character ('%c') at position %s",
                          (unsigned char) ch,
                          pos(here()));
                    exit(3); // unreachable
                }
                add_to_read_buffer(ch);
                ch = read_ch(false);
            }
            // Add 0-terminator
            add_to_read_buffer(0);
            // Check whether macro name
            if (*read_buffer == '&') {
                minion_value* mm = find_macro(read_buffer);
                if (mm) {
                    // Push to remember stack, marking it as not the owner of
                    // its data
                    remember(
                        (minion_value) {mm->type, (short)(mm->flags + F_MACRO_VALUE), mm->size, mm->data});
                    result = mm->type;
                    break;
                }
                // An undefined macro name
                new_String(read_buffer, T_NoType, F_Macro);
                result = F_Macro;
                break;
            }
            // A String without delimiters
            new_String(read_buffer, T_String, F_Simple_String);
            result = T_String;
            break;
        }

        // Look for start of next item
        if (ch == 0) {
            result = F_Token_End; // end of input, no next item
            break;
        }
        if (ch == ' ' || ch == '\n') {
            continue; // continue seeking start of item
        }

        if (ch == '#') {
            // Start comment
            ch = read_ch(false);
            if (ch == '[') {
                // Extended comment: read to "]#"
                position comment_pos = here();
                ch = read_ch(false);
                while (true) {
                    if (ch == ']') {
                        ch = read_ch(false);
                        if (ch == '#') {
                            break;
                        }
                        continue;
                    }
                    if (ch == 0) {
                        error("Unterminated comment ('\\[ ...') at position %s", pos(comment_pos));
                        exit(3); // unreachable
                    }
                    // Comment loop ... read next character
                    ch = read_ch(false);
                }
                // End of extended comment
            } else {
                // "Normal" comment: read to end of line
                while (true) {
                    if (ch == '\n' || ch == 0) {
                        break;
                    }
                    ch = read_ch(false);
                }
            }
            continue; // continue seeking item
        }
        // Delimited string
        if (ch == '"') {
            result = get_string();
            break;
        }
        // list
        if (ch == '[') {
            result = get_list();
            break;
        }
        // map
        if (ch == '{') {
            result = get_map();
            break;
        }
        // further structural symbols
        if (ch == ']') {
            result = F_Token_ListEnd;
            break;
        }
        if (ch == '}') {
            result = F_Token_MapEnd;
            break;
        }
        if (ch == ':') {
            result = F_Token_Colon;
            break;
        }
        if (ch == ',') {
            result = F_Token_Comma;
            break;
        }
        add_to_read_buffer(ch); // start undelimited string
    } // End of item-seeking loop
    return result;
}

minion_doc minion_read(
    const char* input)
{
    if (macros) {
        fputs("[BUG] macros list not cleared", stderr);
        exit(100);
    }
    ch_pointer0 = input;
    ch_pointer = input;
    ch_linestart = input;
    line_index = 0;
    if (setjmp(recover)) {
        // Free redundant malloced items
        for (int i = 0; i < remembered_items_index; ++i) {
            free_item(remembered_items[i]);
        }
        remembered_items_index = 0;
        // Free any macros
        free_macros(macros);
        macros = NULL;
        // Prepare error message
        new_String(error_message, T_NoType, F_Error);
        minion_value m = *remembered_items;
        remembered_items_index = 0;
        return (minion_doc) {{T_NoType, F_NoFlags, 0, 0}, m, NULL};
    }

    while (true) {
        position current_position = here();
        short mtype = get_item();
        if (mtype != F_Macro) {
            if (last_item().flags & F_MACRO_VALUE) {
                error("Position %s: macro value at top level ... redefining?",
                      pos(current_position));
                exit(3); // unreachable
            } else if (real_minion_value(mtype)) {
                // found document item
                break;
            }
            // Invalid item
            if (mtype == F_Token_End) {
                error("Document contains no main item");
                exit(3); // unreachable
            }
            error("Invalid minion item at position %s", pos(current_position));
            exit(3); // unreachable
        }
        // *** macro name: read the definition ***
        // Check for duplicate
        current_position = here();
        // expect ':'
        mtype = get_item();
        if (mtype != F_Token_Colon) {
            error("Expecting ':' in macro definition at position %s", pos(current_position));
            exit(3); // unreachable
        }
        current_position = here();
        mtype = get_item();
        // expect value
        if (real_minion_value(mtype)) {
            // expect ','
            mtype = get_item();
            if (mtype == F_Token_Comma) {
                // Add the macro, taking on ownership of the
                // allocated memory
                minion_value mname = remembered_items[0];
                minion_value mval = remembered_items[1];
                remembered_items_index = 0;
                macro_node* a = (macro_node*) malloc(sizeof(macro_node));
                if (!a)
                    exit(1);
                *a = (macro_node) {(char*) mname.data, macros, mval};
                macros = a;
                continue;
            }
            error("After macro definition: expecting ',' at position %s", pos(current_position));
            exit(3); // unreachable
        }
        error("In macro definition, expecting a value at position %s", pos(current_position));
        exit(3); // unreachable
    }
    // "Real" minion item, not macro definition => document content
    minion_value m = *remembered_items;
    remembered_items_index = 0;
    minion_doc doc = {m, {T_NoType, F_NoFlags, 0, NULL}, macros};
    macros = NULL;

    // Check that there are no further items
    position current_position = here();
    if (get_item() != F_Token_End) {
        minion_free(doc);
        error("Position %s: unexpected item after document item", pos(current_position));
        exit(3); // unreachable
    }

    return doc;
    // NOTE:
    // The result will need to be freed with minion_free() at some point
    // by the caller.
    // Also the buffers error_message, read_buffer and remembered_items
    // have malloced memory, which should be freed (the normal free()) if
    // they are no longer needed – see function minion_tidy().
}

char* minion_error(
    minion_doc doc)
{
    return (char*) (doc.error.flags == F_Error ? doc.error.data : NULL);
}

void dump_string(
    const char* source)
{
    dump_ch('"');
    int i = 0;
    unsigned char ch;
    while (true) {
        ch = source[i++];
        switch (ch) {
        case '"':
            dump_ch('\\');
            dump_ch('"');
            break;
        case '\n':
            dump_ch('\\');
            dump_ch('n');
            break;
        case '\t':
            dump_ch('\\');
            dump_ch('t');
            break;
        case '\b':
            dump_ch('\\');
            dump_ch('b');
            break;
        case '\f':
            dump_ch('\\');
            dump_ch('f');
            break;
        case '\r':
            dump_ch('\\');
            dump_ch('r');
            break;
        case '\\':
            dump_ch('\\');
            dump_ch('\\');
            break;
        case 127:
            dump_ch('\\');
            dump_ch('u');
            dump_ch('0');
            dump_ch('0');
            dump_ch('7');
            dump_ch('F');
            break;
        default:
            if (ch >= 32) {
                dump_ch(ch);
            } else if (ch == 0) {
                // The string is 0-terminated, so \u0000 is not
                // possible as a character.
                dump_ch('"');
                return;
            } else {
                dump_ch('\\');
                dump_ch('u');
                dump_ch('0');
                dump_ch('0');
                if (ch >= 16) {
                    dump_ch('1');
                    ch -= 16;
                } else
                    dump_ch('0');
                if (ch >= 10)
                    dump_ch('A' + ch - 10);
                else
                    dump_ch('0' + ch);
            }
        }
    }
}

static int indent = 2; // pretty-print indentation

bool dump_value(minion_value source, int depth);

void dump_pad(
    int n)
{
    if (n >= 0) {
        dump_ch('\n');
        while (n > 0) {
            dump_ch(' ');
            --n;
        }
    }
}

bool dump_list(
    minion_value source, int depth)
{
    int pad = -1;
    int new_depth = -1;
    if (depth >= 0)
        new_depth = depth + 1;
    pad = new_depth * indent;
    dump_ch('[');
    for (msize i = 0; i < source.size; ++i) {
        dump_pad(pad);
        if (!dump_value(((minion_value*) source.data)[i], new_depth))
            return false;
        dump_ch(',');
    }
    undump_ch();
    dump_pad(depth * indent);
    dump_ch(']');
    return true;
}

bool dump_map(
    minion_value source, int depth)
{
    int pad = -1;
    int new_depth = -1;
    if (depth >= 0)
        new_depth = depth + 1;
    pad = new_depth * indent;
    dump_ch('{');
    for (msize i = 0; i < source.size; ++i) {
        dump_pad(pad);
        minion_pair mp = ((minion_pair*) source.data)[i];
        if (mp.key.type != T_String)
            return false;
        dump_string((char*) mp.key.data);
        dump_ch(':');
        if (depth >= 0)
            dump_ch(' ');
        if (!dump_value(mp.value, new_depth))
            return false;
        dump_ch(',');
    }
    undump_ch();
    dump_pad(depth * indent);
    dump_ch('}');
    return true;
}

bool dump_value(
    minion_value source, int depth)
{
    bool ok = true;
    switch (source.type) {
    case T_String:
        // Strings don't receive any extra formatting
        dump_string((char*) source.data);
        break;
    case T_Array:
        ok = dump_list(source, depth);
        break;
    case T_PairArray:
        ok = dump_map(source, depth);
        break;
    default:
        ok = false;
    }
    return ok;
}

char* minion_dump(
    minion_value source, int depth)
{
    clear_dump_buffer();
    if (dump_value(source, depth)) {
        dump_ch(0);
        return dump_buffer;
    }
    return 0;
}
