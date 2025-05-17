#include "minion.h"

namespace minion {

typedef enum {
    // The "real" types must correspond to their indexes in the
    // MinionValue variant.
    T_NoType = 0,
    T_String,
    T_Array,
    T_PairArray,
    T_Real_End,  // start of structural tokens
    T_Macro,     // an undefined macro
    T_Token_End, // end of data
    T_Token_String_Delim,
    T_Token_ListStart,
    T_Token_ListEnd,
    T_Token_MapStart,
    T_Token_MapEnd,
    T_Token_Comma,
    T_Token_Colon,
} minion_type;

/*
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
    return (flags == T_Error);
}
*/

/* *** Memory management ***
 * The Minion class manages memory allocation for MINION items and the
 * buffers needed for parsing and building them. A MinionValue is thus
 * dependent on the Minion instance used to build it.
 */

/* *** Data sharing ***
 * To reduce allocations and deallocations the actual data is referenced
 * by pointers, the data-owning pointers being kept in a stack by the
 * Minion instance. The data can then be shared by means of its address.
 * 
 * *** Macros ***
 * Macros use the shared-data feature to avoid having to copy their data.
 * Immediately after definition they are stored as normal MinionValues in
 * the data stack, and their addresses are also stored in some sort of map
 * so that they can be accessed by name. When a reference
 * to a macro is found, its MinionValue address can be got from the map.
 */

/* The "get_" family of functions reads the corresponding item from the
 * input. If the result is a minion item, that will be placed on the
 * remember stack. The "get_item" function reads the next item of any
 * kind, returning its type.
 */

/* Read the next "item" from the input.
 * Return the minion_type of the item read, which may be a string, a
 * macro name, an "array" (list) or an "object" (map). If the input is
 * invalid, a MinionError exception will be thrown, containing a message.
 * Also the structural symbols have types, though they have no
 * associated data.
 * 
 * Strings (and macro names) are read into a buffer, which grows if it is
 * too small. Compound items are constructed by reading their components
 * onto a stack, remembered_items.
*/
int Minion::get_item()
{
    char ch;
    read_buffer.clear();
    int result;
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
                result = T_Macro;
                break;
            }
            // A string without delimiters, push to data and remember stacks
            auto m = new MinionValue(read_buffer);
            data.emplace_back(std::unique_ptr<MinionValue>(m));
            remembered_items.push_back(m);
            result = T_String;
            break;
        }

        // Look for start of next item
        if (ch == 0) {
            result = T_Token_End; // end of input, no next item
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
            get_string();
            result = T_String;
            break;
        }
        // list
        if (ch == '[') {
            get_list();
            result = T_PairArray;
            break;
        }
        // map
        if (ch == '{') {
            get_map();
            result = T_PairArray;
            break;
        }
        // further structural symbols
        if (ch == ']') {
            result = T_Token_ListEnd;
            break;
        }
        if (ch == '}') {
            result = T_Token_MapEnd;
            break;
        }
        if (ch == ':') {
            result = T_Token_Colon;
            break;
        }
        if (ch == ',') {
            result = T_Token_Comma;
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
 * Push the string as a MinionValue* to the data and remember stacks.
 */
void Minion::get_string()
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
                break; // unreachable
            case 'U':
                if (add_unicode_to_read_buffer(6))
                    continue;
                error(
                    std::string("Invalid unicode escape in string, position ").append(pos(here())));
                break; // unreachable
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
    auto m = new MinionValue(read_buffer);
    data.emplace_back(std::unique_ptr<MinionValue>(m));
    remembered_items.push_back(m);
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

void Minion::get_list()
{
    int start_index = remembered_items.size();
    position current_pos = here();
    short mtype = get_item();
    while (true) {
        // ',' before the closing bracket is allowed
        if (mtype < T_Real_End) { // TODO: Is 0 possible?
            current_pos = here();
            mtype = get_item();
            if (mtype == T_Token_ListEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item();
                continue;
            }
            error(std::string("Reading list, expecting ',' or ']' at position ")
                      .append(pos(current_pos)));
        }
        if (mtype == T_Token_ListEnd)
            break;
        if (mtype == T_Macro) {
            try {
                auto mm = macros.at(read_buffer);
                // Push to remember stack
                remembered_items.push_back(mm);
                mtype = mm->index();
                continue;
            } catch (const std::out_of_range& e) {
                error(std::string("Undefined macro name at position ").append(pos(current_pos)));
            }
        }
        error(std::string("Expecting list item or ']' at position ").append(pos(current_pos)));
    }
    int end = remembered_items.size();
    auto m = new MinionValue{MinionList()};
    auto ml = std::get_if<MinionList>(m);
    ml->reserve(end - start_index);
    for (int i = start_index; i < end; ++i) {
        ml->push_back(remembered_items[i]);
    }
    data.emplace_back(std::unique_ptr<MinionValue>(m));
    //remembered_items.erase(remembered_items.begin() + start_index, remembered_items.end());
    remembered_items.resize(start_index);
    remembered_items.push_back(m);
}

void Minion::get_map()
{
    int start_index = remembered_items.size();
    position current_pos = here();
    short mtype = get_item();
    while (true) {
        // ',' before the closing bracket is allowed
        // expect key
        if (mtype == T_String) {
            // Check uniqueness of key (end before this key!)
            int end = remembered_items.size() - 1;
            for (int i = start_index; i < end; i += 2) {
                auto si = std::get_if<std::string>(remembered_items[i]);
                if (*si == read_buffer) {
                    error(std::string("Map key has already been defined: ")
                              .append(read_buffer)
                              .append(" ... current position ")
                              .append(pos(current_pos)));
                }
            }
            current_pos = here();
            mtype = get_item();
            // expect ':'
            if (mtype != T_Token_Colon) {
                error(
                    std::string("Expecting ':' in Map item at position ").append(pos(current_pos)));
            }
            current_pos = here();
            mtype = get_item();
            // expect value
            if (mtype == T_Macro) {
                try {
                    auto mm = macros.at(read_buffer);
                    // Push to remember stack
                    remembered_items.push_back(mm);
                    mtype = mm->index();
                } catch (const std::out_of_range& e) {
                    error(std::string("Expecting map value, undefined macro name at position ")
                              .append(pos(current_pos)));
                }
            } else if (mtype >= T_Real_End) { // TODO: Is 0 possible?
                error(std::string("Expecting map value at position ").append(pos(current_pos)));
            }

            current_pos = here();
            mtype = get_item();
            if (mtype == T_Token_MapEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item();
                continue;
            }
            error(std::string("Reading map, expecting ',' or '}' at position ")
                      .append(pos(current_pos)));
        } else if (mtype == T_Token_MapEnd) {
            break;
        }
        error(std::string("Reading map, expecting a key at position ").append(pos(current_pos)));
    }
    int end = remembered_items.size();
    auto m = new MinionValue{MinionMap()};
    auto ml = std::get_if<MinionMap>(m);
    ml->reserve((end - start_index) / 2);
    for (int i = start_index; i < end; ++i) {
        auto mk = std::get_if<std::string>(remembered_items[i]);
        ml->push_back({mk, remembered_items[++i]});
    }
    data.emplace_back(std::unique_ptr<MinionValue>(m));
    //remembered_items.erase(remembered_items.begin() + start_index, remembered_items.end());
    remembered_items.resize(start_index);
    remembered_items.push_back(m);
}

void Minion::error(
    std::string_view msg)
{
    // Add most recently read characters
    int ch_start = 0;
    int recent = ch_index - ch_start;
    if (recent > 80) {
        ch_start = ch_index - 80;
        // Find start of utf-8 sequence
        while (true) {
            unsigned char ch = input_string[ch_start];
            if (ch < 0x80 || (ch >= 0xC0 && ch < 0xF8))
                break;
            ++ch_start;
        }
        recent = ch_index - ch_start;
    }
    auto mx = std::string{msg}.append("\n ... ").append(&input_string[ch_start], recent);
    throw MinionError(mx);
}

// *** Serializing MINION ***

/* Some allocated memory can be retained between minion_read calls,
 * but it should probably be freed sometime, if it is really no
 * longer needed.
*/
/*
// The dump memory can be freed on its own.
void Minion::tidy_dump()
{
    free(dump_buffer);
    dump_buffer = 0;
    dump_buffer_size = 0;
    dump_buffer_index = 0;
}

/*
// Build a new minion string item from the given char*.
// The source bytes are copied, including the trailing 0.
// The "simple" value allows a special flag to be set for undelimited
// strings (relevant for macro names).
MinionValue::MinionValue(const char* text, bool simple)
    : type{T_String}
    , flags{static_cast<short>(simple ? T_Simple_String : T_NoFlags)}
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
/*
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
    flags = T_NoFlags;
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
*/

position Minion::here()
{
    return {line_index + 1, static_cast<int>((ch_index - ch_linestart))};
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
    if (static_cast<size_t>(ch_index) >= input_string.size())
        return 0;
    char ch = input_string.at(ch_index);
    ++ch_index;
    if (ch == '\n') {
        ++line_index;
        ch_linestart = ch_index;
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
    if (ch_index == 0) {
        error("[BUG] unread_ch reached start of data");
    }
    --ch_index;
    //NOTE: '\n' is never unread!
}

MinionValue* Minion::read(
    std::string_view input)
{
    data.clear();
    macros.clear();
    remembered_items.clear();

    input_string = input;
    ch_index = 0;
    ch_linestart = 0;
    line_index = 0;

    while (true) {
        position current_pos = here();
        short mtype = get_item();
        if (mtype == T_Macro) {
            // Check for duplicate
            if (macros.contains(read_buffer)) {
                error(std::string("Position ")
                          .append(pos(current_pos))
                          .append(": macro name already defined"));
            }
            auto mkey = new MinionValue{read_buffer};
            data.emplace_back(std::unique_ptr<MinionValue>(mkey));
            current_pos = here();
            // expect ':'
            mtype = get_item();
            if (mtype != T_Token_Colon) {
                error(std::string("Expecting ':' in macro definition at position ")
                          .append(pos(current_pos)));
            }
            current_pos = here();
            mtype = get_item();
            // expect value
            if (mtype < T_Real_End) { // TODO: Is 0 possible?
                // expect ','
                mtype = get_item();
                if (mtype == T_Token_Comma) {
                    // Add the macro
                    macros[*get_if<std::string>(mkey)] = remembered_items.back();
                    remembered_items.clear();
                    continue;
                }
                error(std::string("After macro definition: expecting ',' at position ")
                          .append(pos(current_pos)));
            }
            error(std::string("In macro definition, expecting a value at position ")
                      .append(pos(current_pos)));
        }
        if (mtype < T_Real_End) { // TODO: Is 0 possible?
            // found document item
            break;
        }
        // Invalid item
        if (mtype == T_Token_End) {
            error("Document contains no main item");
        }
        error(std::string("Invalid minion item at position ").append(pos(current_pos)));
    }
    // "Real" minion item, not macro definition, found.
    // This should be the document content.
    auto m = remembered_items.back();
    remembered_items.clear();

    //TODO: unused macros may be an error?

    // Check that there are no further items
    position current_position = here();
    if (get_item() != T_Token_End) {
        error(std::string("Position ")
                  .append(pos(current_position))
                  .append(": unexpected item after document item"));
    }
    return m;
}

void Minion::dump_string(
    const std::string_view source)
{
    dump_buffer.push_back('"');
    for (unsigned char ch : source) {
        switch (ch) {
        case '"':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('"');
            break;
        case '\n':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('n');
            break;
        case '\t':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('t');
            break;
        case '\b':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('b');
            break;
        case '\f':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('f');
            break;
        case '\r':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('r');
            break;
        case '\\':
            dump_buffer.push_back('\\');
            dump_buffer.push_back('\\');
            break;
        case 127:
            dump_buffer.push_back('\\');
            dump_buffer.push_back('u');
            dump_buffer.push_back('0');
            dump_buffer.push_back('0');
            dump_buffer.push_back('7');
            dump_buffer.push_back('F');
            break;
        default:
            if (ch >= 32) {
                dump_buffer.push_back(ch);
            } else {
                dump_buffer.push_back('\\');
                dump_buffer.push_back('u');
                dump_buffer.push_back('0');
                dump_buffer.push_back('0');
                if (ch >= 16) {
                    dump_buffer.push_back('1');
                    ch -= 16;
                } else
                    dump_buffer.push_back('0');
                if (ch >= 10)
                    dump_buffer.push_back('A' + ch - 10);
                else
                    dump_buffer.push_back('0' + ch);
            }
        }
    }
    dump_buffer.push_back('"');
}

void Minion::dump_pad(
    int n)
{
    if (n >= 0) {
        dump_buffer.push_back('\n');
        while (n > 0) {
            dump_buffer.push_back(' ');
            --n;
        }
    }
}

bool Minion::dump_list(
    const MinionList* source, int depth)
{
    dump_buffer.push_back('[');
    int len = source->size();
    if (len != 0) {
        int pad = -1;
        int new_depth = -1;
        if (depth >= 0)
            new_depth = depth + 1;
        pad = new_depth * indent;
        for (int i = 0; i < len; ++i) {
            dump_pad(pad);
            if (!dump_value(source->at(i), new_depth))
                return false;
            dump_buffer.push_back(',');
        }
        dump_buffer.pop_back();
        dump_pad(depth * indent);
    }
    dump_buffer.push_back(']');
    return true;
}

bool Minion::dump_map(
    const MinionMap* source, int depth)
{
    dump_buffer.push_back('{');
    int len = source->size();
    if (len != 0) {
        int pad = -1;
        int new_depth = -1;
        if (depth >= 0)
            new_depth = depth + 1;
        pad = new_depth * indent;
        for (int i = 0; i < len; ++i) {
            dump_pad(pad);
            auto m = source->at(i);
            dump_string(*m.key);
            dump_buffer.push_back(':');
            if (depth >= 0)
                dump_buffer.push_back(' ');
            if (!dump_value(m.value, new_depth))
                return false;
            dump_buffer.push_back(',');
        }
        dump_buffer.pop_back();
        dump_pad(depth * indent);
    }
    dump_buffer.push_back('}');
    return true;
}

bool Minion::dump_value(
    const MinionValue* source, int depth)
{
    if (const auto s = std::get_if<std::string>(source)) {
        dump_string(*s);
    } else if (const auto l = std::get_if<MinionList>(source)) {
        dump_list(l, depth);
    } else if (const auto m = std::get_if<MinionMap>(source)) {
        dump_map(m, depth);
    } else {
        return false;
    }
    return true;
}

const char* Minion::dump(
    MinionValue* source, int pretty)
{
    int depth = -1;
    if (pretty >= 0) {
        depth = 0;
        indent = pretty;
    }
    dump_buffer.clear();
    if (dump_value(source, depth)) {
        return dump_buffer.c_str();
    }
    return 0;
}

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
