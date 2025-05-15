#include "minion.h"
#include <initializer_list>

namespace minion {

/* All the values in minion_Flags (apart from the null F_NoFlags) are
 * greater than the highest value in minion_Type, so that when type is
 * T_NoType the flags value can be used instead without ambiguity.
*/
#define MIN_FLAG 8
typedef enum {
    F_NoFlags = 0,
    F_Simple_String = MIN_FLAG, // undelimited string
    F_Error,                    // TODO: unused in this version?
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
    //F_NOT_OWNER = 32 // unused in this version?
} minion_Flags;

typedef enum {
    T_NoType = 0,
    T_String,
    T_Array,
    T_PairArray,
} minion_Type;

// Build a null MinionValue.
MinionValue::MinionValue()
    : type{T_NoType}
    , flags{F_NoFlags}
    , data_index{0}
{}

// Build a MinionValue from explicit field values.
MinionValue::MinionValue(
    short type, short flags, int data)
    : type{type}
    , flags{flags}
    , data_index{data}
{}

bool MinionValue::is_string()
{
    return (type == T_String);
}

bool MinionValue::is_list()
{
    return (type == T_Array);
}

bool MinionValue::is_map()
{
    return (type == T_PairArray);
}

//TODO?
bool MinionValue::is_error()
{
    return (flags == F_Error);
}

/* *** Memory management ***
 * The Minion class manages memory allocation for MINION items and the
 * buffers needed for parsing and building them. A MinionValue is thus
 * dependent on the Minion instance used to build it.
 */

/* *** Data sharing ***
 * To reduce allocations and deallocations the actual data is referenced
 * by pointers, accessed by indexes into a pointer vector, so that data
 * can be shared.
 * 
 * TODO ??? MinionValue items have a data field which is a pointer to
 * the actual data (which can currently be a string – 0-terminated character
 * array – or an array of MinionValue items). Standard C++ strings and vectors
 * are not used at present because they are a bit more "bulky" and are not
 * really designed for this sort of use. Ownership of the referenced data is
 * marked by a flag (or rather its absence!) in the MinionValue item flags
 * field.
 *
 * *** Macros ***
 * Macros use the shared-data feature to avoid having to copy their data.
 * Immediately after definition they are stored as normal MinionValues in
 * some sort of map so that they can be accessed by name. When a reference
 * to a macro is found, its MinionValue will be copied, but not the memory
 * referenced by the data field (i.e. it is a shallow copy). As the
 * structure is read-only, this should be no problem, the data fields are
 * just shared.
 */

std::map<std::string_view, MinionValue> macros;

/* Read the next "item" from the input.
 * Return the minion_Type of the item read, which may be a string, an
 * "array" (list) or an "object" (map). If the input is invalid, a
 * MinionError exception will be thrown, containing a message.
 * Also the structural symbols have types, available via the flags field.
 * 
 * Strings are read into a buffer, which grows if it is too small.
 * Compound items are constructed by reading their components onto a stack.
*/
int Minion::get_item()
{
    char ch;
    read_buffer.clear();
    short result;
    while (true) {
        ch = read_ch(false);
        if (read_buffer.size() != 0) {
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
                    auto s = std::string("Unexpected character ('");
                    s.push_back(ch);
                    error(s.append("' at position ").append(pos(here())));
                }
                read_buffer.push_back(ch);
                ch = read_ch(false);
            }
            // Check whether macro name
            if (read_buffer[0] == '&') {
                if (macros.contains(read_buffer)) {
                    // Push to remember stack
                    auto mm = macros.at(read_buffer);
                    remembered_items.push_back(mm);
                    result = mm.type;
                    break;
                }
                // An undefined macro name, available in read_buffer
                result = F_Macro;
                break;
            }
            // A String without delimiters
            int i = data.size();
            data.push_back({new std::string(read_buffer)});
            MinionValue m{T_String, F_Simple_String, i};
            remembered_items.push_back(m);
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
                        error(std::string("Unterminated comment ('\\[ ...') at position ")
                                  .append(pos(comment_pos)));
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
        read_buffer.push_back(ch); // start undelimited string
    } // End of item-seeking loop
    return result;
}

/* Read a delimited string (terminated by '"') from the input.
 *
 * It is entered after the initial '"' has been read, so the next character
 * will be the first of the string.
 *
 * Escapes, introduced by '\', are possible. These are an extension of the
 * JSON escapes – see the MINION specification.
 *
 * Return the string as a MinionValue on the remember stack.
 */
int Minion::get_string()
{
    position start_pos = here();
    char ch;
    while (true) {
        ch = read_ch(true);
        if (ch == '"')
            break;
        if (ch == 0) {
            error(std::string("End of data reached inside delimited string from position ")
                      .append(pos(start_pos)));
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
                error(
                    std::string("Invalid unicode escape in string, position ").append(pos(here())));
            case 'U':
                if (add_unicode_to_read_buffer(6))
                    continue;
                error(
                    std::string("Invalid unicode escape in string, position ").append(pos(here())));
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
                            error(std::string(
                                      "End of data reached inside string comment from position ")
                                      .append(pos(comment_pos)));
                        }
                        // loop with next character
                        ch = read_ch(false);
                    }
                }
                continue; // comment ended, seek next character
            default:
                error(std::string("Illegal string escape at position ").append(pos(here())));
            }
        }
        read_buffer.push_back(ch);
    }
    int i = data.size();
    data.push_back({new std::string(read_buffer)});
    MinionValue m{T_String, F_NoFlags, i};
    remembered_items.push_back(m);
    return T_String;
}

// Convert a unicode code point (as hex string) to a UTF-8 string
bool Minion::add_unicode_to_read_buffer(
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
        read_buffer.push_back(code_point);
    } else if (code_point <= 0x7FF) {
        read_buffer.push_back((code_point >> 6) | 0xC0);
        read_buffer.push_back((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0xFFFF) {
        read_buffer.push_back((code_point >> 12) | 0xE0);
        read_buffer.push_back(((code_point >> 6) & 0x3F) | 0x80);
        read_buffer.push_back((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0x10FFFF) {
        read_buffer.push_back((code_point >> 18) | 0xF0);
        read_buffer.push_back(((code_point >> 12) & 0x3F) | 0x80);
        read_buffer.push_back(((code_point >> 6) & 0x3F) | 0x80);
        read_buffer.push_back((code_point & 0x3F) | 0x80);
    } else {
        // Invalid input
        return false;
    }
    return true;
}

bool real_minion_value(
    short mtype)
{
    return (mtype != T_NoType && mtype < MIN_FLAG);
}

int Minion::get_list()
{
    int start_index = remembered_items.size();
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
            error(std::string("Reading list, expecting ',' or ']' at position ")
                      .append(pos(current_pos)));
        }
        if (mtype == F_Macro) {
            error(std::string("Undefined macro name at position ").append(pos(current_pos)));
        } else {
            error(std::string("Expecting list item or ']' at position ").append(pos(current_pos)));
        }
    }
    auto m = new std::vector<MinionValue>;
    int end = remembered_items.size();
    m->reserve(end - start_index);
    for (int i = start_index; i < end; ++i) {
        m->push_back(remembered_items[i]);
    }
    int i = data.size();
    data.push_back({.l = m});
    remembered_items.erase(remembered_items.begin() + start_index, remembered_items.end());
    //??? remembered_items.resize(start_index);
    remembered_items.push_back({T_Array, F_NoFlags, i});
    return T_Array;
}

int Minion::get_map()
{
    int start_index = remembered_items.size();
    position current_pos = here();
    short mtype = get_item();
    std::string seeking;
    while (true) {
        // ',' before the closing bracket is allowed
        if (mtype == F_Token_MapEnd)
            break;
        // expect key
        if (mtype == T_String) {
            // Check uniqueness of key (end before this key!)
            int end = remembered_items.size() - 1;
            for (int i = start_index; i < end; i += 2) {
                auto si = remembered_items[i].data_index;
                if (*data[si].s == read_buffer) {
                    error(std::string("Map key has already been defined: ")
                              .append(read_buffer)
                              .append(" ... current position ")
                              .append(pos(current_pos)));
                }
            }
            current_pos = here();
            mtype = get_item();
            // expect ':'
            if (mtype != F_Token_Colon) {
                error(
                    std::string("Expecting ':' in Map item at position ").append(pos(current_pos)));
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
                error(std::string("Reading map, expecting ',' or '}' at position ")
                          .append(pos(current_pos)));
            } else if (mtype == F_Macro) {
                seeking = "Expecting map value, undefined macro name at position %s";
            }
        } else {
            seeking = "Reading map, expecting a key at position %s";
        }
        error(seeking.append(pos(current_pos)));
    }
    auto m = new std::vector<map_pair>;
    int end = remembered_items.size();
    m->reserve((end - start_index) / 2);
    for (int i = start_index; i < end; ++i) {
        auto si = remembered_items[i].data_index;
        m->push_back({data[si].s, remembered_items[++i]});
    }
    int i = data.size();
    data.push_back({.m = m});
    remembered_items.erase(remembered_items.begin() + start_index, remembered_items.end());
    //??? remembered_items.resize(start_index);
    remembered_items.push_back({T_PairArray, F_NoFlags, i});
    return T_PairArray;
}

//////////////

void Minion::dump_ch(
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
void Minion::undump_ch()
{
    dump_buffer_index--;
}

void Minion::error(
    std::string_view msg)
{
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
    auto mx = std::string{msg}.append(
        ch_start, recent);
    throw MinionError(mx);
}

// Free the memory used for a minion item.
MinionValue::~MinionValue()
{
    if (size == 0 || (flags & F_NOT_OWNER) != 0)
        return;
    if (type == T_Array) {
        MinionValue* p = (MinionValue*) data;
        int n = size;
        for (int i = 0; i < n; ++i) {
            delete (&p[i]);
        }
    } else if (type == T_PairArray) {
        MinionValue* p = (MinionValue*) data;
        int n = size * 2;
        for (int i = 0; i < n; ++i) {
            delete (&p[i]);
        }
    }
    // Free the memory pointed to directly by the data field. This will
    // collect the actual array storage from the above items or the
    // character storage for strings, etc.
    free(data);
}

void free_macros(
    macro_node* mp)
{
    while (mp) {
        free(mp->name);
        delete (&mp->value);
        macro_node* mp0 = mp;
        mp = mp->next;
        free(mp0);
    }
}

MinionValue* Minion::find_macro(
    char* name)
{
    macro_node* mp = macros;
    while (mp) {
        if (strcmp(name, mp->name) == 0) {
            return &mp->value;
        }
        mp = mp->next;
    }
    return nullptr;
}

/* Some allocated memory can be retained between minion_read calls,
 * but it should probably be freed sometime, if it is really no
 * longer needed.
*/
// The dump memory can be freed on its own.
void Minion::tidy_dump()
{
    free(dump_buffer);
    dump_buffer = 0;
    dump_buffer_size = 0;
    dump_buffer_index = 0;
}

void Minion::remember(
    MinionValue minion_item)
{
    if (remembered_items_index == remembered_items_size) {
        // Need more space
        int n = remembered_items_size + remembered_items_size_increment;
        MinionValue* tmp = (MinionValue*) realloc(remembered_items, n * sizeof(MinionValue));
        if (tmp == nullptr) {
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

void Minion::release()
{
    for (int i = 0; i < remembered_items_index; ++i) {
        delete (&remembered_items[i]);
    }
    remembered_items_index = 0;
}

// --- END: Keep track of "unbound" malloced items ---

/*
// Build a new minion string item from the given char*.
// The source bytes are copied, including the trailing 0.
// The "simple" value allows a special flag to be set for undelimited
// strings (relevant for macro names).
MinionValue::MinionValue(const char* text, bool simple)
    : type{T_String}
    , flags{static_cast<short>(simple ? F_Simple_String : F_NoFlags)}
{
    size = strlen(text);
    void* s = malloc(sizeof(char) * (++size));
    if (!s)
        exit(1);
    memcpy(s, text,  + 1);
}
*/

// Build a new minion list item from the arguments, which are MinionValues.
// The source data (referenced by the data fields) is not copied, thus
// the new list takes on ownership if the "not-owner" flags of the data
// are clear.
MinionValue Minion::new_array(std::initializer_list<MinionValue> items)
{
    auto start_index = remembered_items_index;
    for (const auto& item : items) {
        remember(item);
    }
    auto m = MinionValue(&remembered_items[start_index],
        remembered_items_index - start_index, false);
    remembered_items_index = start_index;
    return m;
}

// Build a new minion map from the given minion_map items.
// The source data of the value items (referenced by the data fields) is
// not copied, thus the new map takes on ownership if the "not-owner" flags
// of the data are clear.
MinionValue Minion::new_map(std::initializer_list<map_item> items)
{
    auto start_index = remembered_items_index;
    for (const auto& item : items) {
        remember(item.key);
        remember(item.value);
    }
    auto m = MinionValue(&remembered_items[start_index],
        remembered_items_index - start_index, true);
    remembered_items_index = start_index;
    return m;
}


// Free all buffers
Minion::~Minion()
{
    free(read_buffer);
    free(dump_buffer);
    //TODO: Is this enough? What if remembered_items is not empty?
    free(remembered_items);
    //TODO: macros should be freed on exit from read()
}

// Build a new list/map item from the given array of MinionValue items.
// For a map, the items are regarded as pairs (so there must be an even
// number) of which the first item must be a string.
// The len argument is always the number of MinionValue items.
MinionValue::MinionValue(MinionValue* items, int len, bool map)
{
    if (len < 0) {
        throw MinionError(
            "In MinionValue list/map constructor: negative length");
    }
    if (map) {
        if ((len & 1) != 0) {
            throw MinionError(
                "In MinionValue map constructor: odd number of elements");
        }
        // Check that all keys are strings
        for (int i = 0; i < len; i += 2) {
            if (items[i].type != T_String) {
            throw MinionError(
                "In MinionValue map constructor: key not string");
           }
        }
        type = T_PairArray;
        size = len / 2;
    } else {
        type = T_Array;
        size = len;
    }
    flags = F_NoFlags;
    void* a = 0;
    if (len > 0) {
        size_t nbytes = sizeof(MinionValue) * len;
        a = malloc(nbytes);
        if (!a)
            exit(1);
        memcpy(a, items, nbytes);
    }
    data = a;
}

position Minion::here()
{
    return {
        line_index + 1,
        static_cast<int>((ch_pointer - ch_linestart))};
}

std::string Minion::pos(
    position p)
{
    return std::to_string(p.line_n) + '.' + std::to_string(p.byte_ix);
}

// Represent number as a string with hexadecimal digits, at least minwidth. 
std::string to_hex(long val, int minwidth)
{
    std::string s;
    if (val >= 16 || minwidth > 1) {
        s = to_hex(val/16, minwidth - 1);
        val %= 16;
    }
    if (val < 10) s.push_back('0' + val);
    else s.push_back('A' + val - 10);
    return s;
}

char Minion::read_ch(
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
        error(std::string("Unexpected newline in delimited string at line ")
            .append(std::to_string(line_index)));
    } else if (ch == '\r' || ch == '\t') {
        // these are acceptable in the source, but not within strings.
        if (!instring) {
            // separator
            return ' ';
        }
    } else if ((unsigned char) ch >= 32 && ch != 127) {
        return ch;
    }
    error(std::string("Illegal character (byte) 0x")
        .append(to_hex(ch, 2))
        .append(" at position ")
        .append(pos(here())));
    return 0; // unreachable
}

void Minion::unread_ch()
{
    if (ch_pointer == ch_pointer0) {
        fputs("[BUG] unread_ch reached start of data", stderr);
        exit(100);
    }
    --ch_pointer;
    //NOTE: '\n' is never unread!
}
// --- END: Handle character-by-character reading ---

// The "get_" family of functions reads the corresponding item from the
// input. If the result is a minion item, that will be placed on the
// remember stack. The "get_" functions return the type of the item that
// was read – the structural tokens have no malloced memory, so they do
// not need to be stacked.

MinionValue Minion::last_item()
{
    if (remembered_items_index) {
        return remembered_items[remembered_items_index - 1];
    }
    fputs("[BUG] 'last_item: remembered' stack corrupted", stderr);
    exit(100);
}

bool Minion::is_key_unique(
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

MinionValue Minion::read(
    std::string_view input)
{
    macros.clear();

    input_string = input;
    ch_index = 0;
    ch_linestart = 0;
    line_index = 0;

    try {
        while (true) {
            position current_position = here();
            short mtype = get_item();
            if (mtype != F_Macro) {
                if (last_item().flags & F_NOT_OWNER) {
                    error(std::string("Position ")
                        .append(pos(current_position))
                        .append(": macro value at top level ... redefining?"));
                } else if (real_minion_value(mtype)) {
                    // found document item
                    break;
                }
                // Invalid item
                if (mtype == F_Token_End) {
                    error("Document contains no main item");
                }
                error(std::string(
                    "Invalid minion item at position ")
                    .append(pos(current_position)));
            }
            // *** macro name: read the definition ***
            // Check for duplicate
            current_position = here();
            // expect ':'
            mtype = get_item();
            if (mtype != F_Token_Colon) {
                error(std::string(
                    "Expecting ':' in macro definition at position ")
                    .append(pos(current_position)));
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
                    MinionValue mname = remembered_items[0];
                    MinionValue mval = remembered_items[1];
                    remembered_items_index = 0;
                    macro_node* a = (macro_node*) malloc(sizeof(macro_node));
                    if (!a)
                        exit(1);
                    *a = (macro_node) {(char*) mname.data, macros, mval};
                    macros = a;
                    continue;
                }
                error(std::string(
                    "After macro definition: expecting ',' at position ")
                    .append(pos(current_position)));
            }
            error(std::string(
                "In macro definition, expecting a value at position ")
                .append(pos(current_position)));
        }
        // "Real" minion item, not macro definition, found.
        // This should be the document content.
        MinionValue m = *remembered_items;
        remembered_items_index = 0;

        // Free the space used by macros.
        //TODO: unused macros may be an error?
        free_macros(macros);
        macros = nullptr;

        //TODO???

        // Check that there are no further items
        position current_position = here();
        if (get_item() != F_Token_End) {
            delete (&m); //TODO: Is this a double delete?
            error(std::string(
                "Position ")
                .append(pos(current_position))
                .append(": unexpected item after document item"));
        }

        return m;
        // NOTE:
        //TODO??? Is the use of delete on MinionValues (their addresses!)
        // correct?
        
        // The result will need to be freed with minion_free() at some point
        // by the caller.
        // Also the buffers error_message, read_buffer and remembered_items
        // have malloced memory, which should be freed (the normal free()) if
        // they are no longer needed – see function minion_tidy().

    } catch (MinionError& e) {
        // Free any remembered items
        for (int i = 0; i < remembered_items_index; ++i) {
            delete (&remembered_items[i]);
        }
        remembered_items_index = 0;
        // Free any macros
        free_macros(macros);
        macros = nullptr;
        throw;
    }
}

void Minion::dump_string(
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

void Minion::dump_pad(
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

bool Minion::dump_list(
    MinionValue source, int depth)
{
    int pad = -1;
    int new_depth = -1;
    if (depth >= 0)
        new_depth = depth + 1;
    pad = new_depth * indent;
    dump_ch('[');
    for (int i = 0; i < source.size; ++i) {
        dump_pad(pad);
        if (!dump_value(((MinionValue*) source.data)[i], new_depth))
            return false;
        dump_ch(',');
    }
    undump_ch();
    dump_pad(depth * indent);
    dump_ch(']');
    return true;
}

bool Minion::dump_map(
    MinionValue source, int depth)
{
    int len = source.size * 2;
    int pad = -1;
    int new_depth = -1;
    if (depth >= 0)
        new_depth = depth + 1;
    pad = new_depth * indent;
    dump_ch('{');
    for (int i = 0; i < len; ++i) {
        dump_pad(pad);
        auto key = static_cast<MinionValue*>(source.data)[i];
        if (key.type != T_String)
            return false;
        dump_string((char*) key.data);
        dump_ch(':');
        if (depth >= 0)
            dump_ch(' ');
        if (!dump_value(static_cast<MinionValue*>(source.data)[++i], new_depth))
            return false;
        dump_ch(',');
    }
    undump_ch();
    dump_pad(depth * indent);
    dump_ch('}');
    return true;
}

bool Minion::dump_value(
    MinionValue source, int depth)
{
    bool ok = true;
    switch (source.type) {
    case T_String:
        // Strings don't receive any extra formatting
        dump_string(static_cast<char*>(source.data));
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

char* Minion::dump(
    MinionValue source, int depth)
{
    clear_dump_buffer();
    if (dump_value(source, depth)) {
        dump_ch(0);
        return dump_buffer;
    }
    return 0;
}

/*??
// *** Construction functions ...

MinionValue Minion::pop_remembered()
{
    return remembered_items[--remembered_items_index];
}
*/

} // End of namespace minion

/* Use of initializer_list:
struct pa{char* k; int v;};
void myFunction(initializer_list<pa> myList)
{

    // Print the size (length) of myList
    cout << "Size of myList: " << myList.size();
    cout << "\n";

    // Print elements of myList
    cout << "Elements of myList: ";

    // iterate to all the values of myList
    for (const auto& value : myList) {

        // Print value at each iteration
        cout << value.k << " ";
    }
}
*/
