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

// Represent number as a string with hexadecimal digits, at least minwidth.
std::string to_hex(
    long val, int minwidth)
{
    std::string s;
    if (val >= 16 || minwidth > 1) {
        s = to_hex(val / 16, minwidth - 1);
        val %= 16;
    }
    if (val < 10)
        s.push_back('0' + val);
    else
        s.push_back('A' + val - 10);
    return s;
}

char InputBuffer::read_ch(
    bool instring)
{
    if (ch_index >= input.size())
        return 0;
    char ch = input.at(ch_index);
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

void InputBuffer::unread_ch()
{
    if (ch_index == 0) {
        error("[BUG] unread_ch reached start of data");
    }
    --ch_index;
    //NOTE: '\n' is never unread!
}

void InputBuffer::error(
    std::string_view msg)
{
    // Add most recently read characters
    int ch_start = 0;
    int recent = ch_index - ch_start;
    if (recent > 80) {
        ch_start = ch_index - 80;
        // Find start of utf-8 sequence
        while (true) {
            unsigned char ch = input[ch_start];
            if (ch < 0x80 || (ch >= 0xC0 && ch < 0xF8))
                break;
            ++ch_start;
        }
        recent = ch_index - ch_start;
    }
    auto mx = std::string{msg}.append("\n ... ").append(&input[ch_start], recent);
    throw MinionError(mx);
}

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
int InputBuffer::get_item(
    MValue& value_buffer)
{
    char ch;
    ch_buffer.clear();
    int result;
    while (true) {
        ch = read_ch(false);
        if (ch_buffer.size() != 0) {
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
                ch_buffer.push_back(ch);
                ch = read_ch(false);
            }
            // Check whether macro name
            if (ch_buffer[0] == '&') {
                result = T_Macro;
                break;
            }
            // A string without delimiters
            value_buffer = std::move(MString(ch_buffer));
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
            value_buffer = std::move(MString(ch_buffer));
            result = T_String;
            break;
        }
        // list
        if (ch == '[') {
            value_buffer = std::move(get_list());
            result = T_PairArray;
            break;
        }
        // map
        if (ch == '{') {
            value_buffer = std::move(get_map());
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
        ch_buffer.push_back(ch); // start undelimited string
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
 * The string is available in ch_buffer
 */
void InputBuffer::get_string()
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
                if (add_unicode_to_ch_buffer(4))
                    continue;
                error(
                    std::string("Invalid unicode escape in string, position ").append(pos(here())));
                break; // unreachable
            case 'U':
                if (add_unicode_to_ch_buffer(6))
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
        ch_buffer.push_back(ch);
    }
}

// Convert a unicode code point (as hex string) to a UTF-8 string
bool InputBuffer::add_unicode_to_ch_buffer(
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
        ch_buffer.push_back(code_point);
    } else if (code_point <= 0x7FF) {
        ch_buffer.push_back((code_point >> 6) | 0xC0);
        ch_buffer.push_back((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0xFFFF) {
        ch_buffer.push_back((code_point >> 12) | 0xE0);
        ch_buffer.push_back(((code_point >> 6) & 0x3F) | 0x80);
        ch_buffer.push_back((code_point & 0x3F) | 0x80);
    } else if (code_point <= 0x10FFFF) {
        ch_buffer.push_back((code_point >> 18) | 0xF0);
        ch_buffer.push_back(((code_point >> 12) & 0x3F) | 0x80);
        ch_buffer.push_back(((code_point >> 6) & 0x3F) | 0x80);
        ch_buffer.push_back((code_point & 0x3F) | 0x80);
    } else {
        // Invalid input
        return false;
    }
    return true;
}

MList InputBuffer::get_list()
{
    MList mlist;
    MValue value_buffer;
    position current_pos = here();
    auto mtype = get_item(value_buffer);
    while (true) {
        // ',' before the closing bracket is allowed
        if (mtype < T_Real_End) { // TODO: Is 0 possible?
            mlist.add(value_buffer);
            current_pos = here();
            mtype = get_item(value_buffer);
            if (mtype == T_Token_ListEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item(value_buffer);
                continue;
            }
            error(std::string("Reading list, expecting ',' or ']' at position ")
                      .append(pos(current_pos)));
        }
        if (mtype == T_Token_ListEnd)
            break;
        if (mtype == T_Macro) {
            try {
                value_buffer = macros.at(ch_buffer);
                mtype = value_buffer.type();
                continue;
            } catch (const std::out_of_range& e) {
                error(std::string("Undefined macro name at position ").append(pos(current_pos)));
            }
        }
        error(std::string("Expecting list item or ']' at position ").append(pos(current_pos)));
    }
    return mlist;
}

MMap InputBuffer::get_map()
{
    MMap mmap;
    MValue value_buffer;
    position current_pos = here();
    auto mtype = get_item(value_buffer);
    while (true) {
        // ',' before the closing bracket is allowed
        // expect key
        if (mtype == T_String) {
            // Check uniqueness of key
            if (mmap.get(ch_buffer, value_buffer)) {
                error(std::string("Map key has already been defined: ")
                          .append(ch_buffer)
                          .append(" ... current position ")
                          .append(pos(current_pos)));
            }
            std::string s = std::move(ch_buffer); // save key
            current_pos = here();
            mtype = get_item(value_buffer);
            // expect ':'
            if (mtype != T_Token_Colon) {
                error(
                    std::string("Expecting ':' in Map item at position ").append(pos(current_pos)));
            }
            current_pos = here();
            mtype = get_item(value_buffer);
            // expect value
            if (mtype == T_Macro) {
                try {
                    value_buffer = macros.at(ch_buffer);
                    mtype = value_buffer.type();
                } catch (const std::out_of_range& e) {
                    error(std::string("Expecting map value, undefined macro name at position ")
                              .append(pos(current_pos)));
                }
            } else if (mtype >= T_Real_End) { // TODO: Is 0 possible?
                error(std::string("Expecting map value at position ").append(pos(current_pos)));
            }
            mmap.add({std::move(s), std::move(value_buffer)});
            current_pos = here();
            mtype = get_item(value_buffer);
            if (mtype == T_Token_MapEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item(value_buffer);
                continue;
            }
            error(std::string("Reading map, expecting ',' or '}' at position ")
                      .append(pos(current_pos)));
        } else if (mtype == T_Token_MapEnd) {
            break;
        }
        error(std::string("Reading map, expecting a key at position ").append(pos(current_pos)));
    }
    return mmap;
}

// *** Serializing MINION ***

/* Some allocated memory can be retained between minion_read calls,
 * but it should probably be freed sometime, if it is really no
 * longer needed.
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
*/

MValue* InputBuffer::read(
    std::string& input_string)
{
    input = input_string;
    ch_index = 0;
    line_index = 0;
    ch_linestart = 0;
    macros.clear();

    while (true) {
        position current_pos = here();
        short mtype = get_item(data);
        if (mtype == T_Macro) {
            // Check for duplicate
            if (macros.contains(ch_buffer)) {
                error(std::string("Position ")
                          .append(pos(current_pos))
                          .append(": macro name already defined"));
            }
            std::string mkey = std::move(ch_buffer);
            current_pos = here();
            // expect ':'
            mtype = get_item(data);
            if (mtype != T_Token_Colon) {
                error(std::string("Expecting ':' in macro definition at position ")
                          .append(pos(current_pos)));
            }
            current_pos = here();
            mtype = get_item(data);
            // expect value
            if (mtype < T_Real_End) { // TODO: Is 0 possible?
                // Add the macro
                macros.emplace(std::move(mkey), std::move(data));
                // expect ','
                mtype = get_item(data);
                if (mtype == T_Token_Comma) {
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

    //TODO: unused macros may be an error?

    // Check that there are no further items
    position current_pos = here();
    if (get_item(data) != T_Token_End) {
        error(std::string("Position ")
                  .append(pos(current_pos))
                  .append(": unexpected item after document item"));
    }
    return &data;
}

void DumpBuffer::dump_string(
    std::string& source)
{
    add('"');
    for (unsigned char ch : source) {
        switch (ch) {
        case '"':
            add('\\');
            add('"');
            break;
        case '\n':
            add('\\');
            add('n');
            break;
        case '\t':
            add('\\');
            add('t');
            break;
        case '\b':
            add('\\');
            add('b');
            break;
        case '\f':
            add('\\');
            add('f');
            break;
        case '\r':
            add('\\');
            add('r');
            break;
        case '\\':
            add('\\');
            add('\\');
            break;
        case 127:
            add('\\');
            add('u');
            add('0');
            add('0');
            add('7');
            add('F');
            break;
        default:
            if (ch >= 32) {
                add(ch);
            } else {
                add('\\');
                add('u');
                add('0');
                add('0');
                if (ch >= 16) {
                    add('1');
                    ch -= 16;
                } else
                    add('0');
                if (ch >= 10)
                    add('A' + ch - 10);
                else
                    add('0' + ch);
            }
        }
    }
    add('"');
}

void DumpBuffer::dump_pad()
{
    if (depth >= 0) {
        add('\n');
        for (int i = 0; i < depth * indent; ++i)
            add(' ');
    }
}

void DumpBuffer::dump_list(
    MList& source)
{
    add('[');
    auto l = source.data;
    int len = l.size();
    if (len != 0) {
        auto d = depth;
        if (depth >= 0)
            ++depth;
        for (int i = 0; i < len; ++i) {
            dump_pad();
            dump_value(l.at(i));
            add(',');
        }
        depth = d;
        pop();
        dump_pad();
    }
    add(']');
}

void DumpBuffer::dump_map(
    MMap& source)
{
    add('{');
    auto m = source.data;
    int len = m.size();
    if (len != 0) {
        auto d = depth;
        if (depth >= 0)
            ++depth;
        for (int i = 0; i < len; ++i) {
            dump_pad();
            auto mp = &m.at(i);
            dump_string(mp->key);
            add(':');
            if (depth >= 0)
                add(' ');
            dump_value(mp->value);
            add(',');
        }
        depth = d;
        pop();
        dump_pad();
    }
    add('}');
}

void DumpBuffer::dump_value(
    MValue& source)
{
    switch (source.type()) {
    case T_String:
        dump_string(static_cast<MString&>(source).data);
        break;
    case T_Array:
        dump_list(static_cast<MList&>(source));
        break;
    case T_PairArray:
        dump_map(static_cast<MMap&>(source));
        break;
    default:
        throw "[BUG] MINION dump: bad MValue type";
    }
}

const char* DumpBuffer::dump(
    MValue& data, int pretty)
{
    depth = -1;
    if (pretty >= 0) {
        depth = 0;
        indent = pretty;
    }
    buffer.clear();
    dump_value(data);
    return buffer.c_str();
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
