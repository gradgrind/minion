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

Diagnostics diagnostics;

MString* Diagnostics::new_string(
    std::string_view s)
{
    auto p = new MString(std::string(s));
    allocs.emplace(p, false);
    return p;
}

MList* Diagnostics::new_list()
{
    auto p = new MList();
    allocs.emplace(p, false);
    return p;
}

MMap* Diagnostics::new_map()
{
    auto p = new MMap();
    allocs.emplace(p, false);
    return p;
}

void Diagnostics::free(
    void* p)
{
    if (allocs.contains(p)) {
        if (allocs.at(p)) {
            throw "[BUG] double free";
        }
        allocs.at(p) = true;
    } else {
        throw "[BUG] unknown alloc";
    }
}

void Diagnostics::analyse()
{
    for (const auto& pb : allocs) {
        if (!pb.second) {
            printf("$$$$$ Unfreed: %lu\n", pb.first);
        }
    }
    allocs.clear();
}

// Test whether a minion_type corresponds to a valid MValue.
// If there is no type, throw an exception (it's a bug).
bool real_type(
    int mtype)
{
    if (mtype < T_Real_End) {
        if (mtype == T_NoType)
            throw "[BUG] Encountered null MValue";
        return true;
    }
    return false;
}

/* *** Memory management ***
 * The Minion class manages memory allocation for MINION items and the
 * buffers needed for parsing and building them. A MinionValue is thus
 * dependent on the Minion instance used to build it.
 */

/* *** Data sharing ***
 * To reduce allocations and deallocations the actual data is stored on
 * the heap and referenced by non-smart pointers. The data can then be
 * passed around, and even shared (in the case of macro substitutions)
 * without incurring extra allocations.
 * When the data is no longer needed it must be released. This is handled
 * by the FreeMinion class, which runs through all the allocated nodes
 * recursively. For this to work, all heap-allocated items which are not
 * deleted separately (e.g. by the normal C++ mechanisms) must end up
 * in the value tree, where they can be reached by the freer.
 * To avoid double freeing of shared nodes (which can arise when macros
 * are used), the freer keeps track of nodes it has already deleted.
 */

/* *** Macros ***
 * Macros use the shared-data feature to avoid having to copy their data.
 * Immediately after definition they are stored as normal MinionValues in
 * the data stack, and their addresses are also stored in some sort of map
 * so that they can be accessed by name. When a reference
 * to a macro is found, its MinionValue address can be got from the map.
 */

//TODO: When parsing, keep track of unused macros so that these can
// be freed .. and maybe flagged as errors?

// +++ Deep copy of MValue +++
// This must build in-place to avoid potential memory leaks.
void MValue::copy(
    MinionValue& m)
{
    mcopy(m);
}

//TODO with diagnostics?
void MValue::mcopy(
    MValue& m)
{
    switch (type) {
    case T_String:
        m = MValue(T_String, new MString(*reinterpret_cast<MString*>(minion_item)));
        break;
    case T_Array: {
        auto mlist = new MList;
        m = {T_Array, mlist};
        auto ml = reinterpret_cast<MList*>(minion_item);
        for (auto& v : *ml) {
            mlist->emplace_back(MValue{});
            MValue& mref = (*mlist)[mlist->size() - 1];
            v.mcopy(mref);
        }
        break;
    };
    case T_PairArray: {
        auto mmap = new MMap;
        m = {T_PairArray, mmap};
        auto mm = reinterpret_cast<MMap*>(minion_item);
        for (auto& mp : *mm) {
            mmap->emplace_back(mp.first, MValue{});
            MPair& mpref = (*mmap)[mmap->size() - 1];
            MValue& mref = mpref.second;
            mp.second.mcopy(mref);
        }
        break;
    };
    default:
        // This is unexpected ...
        throw "[BUG] Invalid MValue whilst copying";
    }
}

void MValue::free()
{
    if (not_owner)
        return;

    if (minion_item)
        diagnostics.free(minion_item);

    switch (type) {
    case T_String:
        delete reinterpret_cast<MString*>(minion_item);
        break;
    case T_Array: {
        auto ml = reinterpret_cast<MList*>(minion_item);
        for (auto& v : *ml) {
            v.free();
        }
        delete ml;
    } break;
    case T_PairArray:
        auto mm = reinterpret_cast<MMap*>(minion_item);
        for (auto& mp : *mm) {
            mp.second.free();
        }
        delete mm;
        break;
    }
    type = T_NoType;
    minion_item = nullptr;
}

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
 * too small. Compound items are constructed while being read.
 */
int InputBuffer::get_item(
    MValue& mvalue)
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
            get_list(mvalue);
            result = T_Array;
            break;
        }
        // map
        if (ch == '{') {
            get_map(mvalue);
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

void InputBuffer::get_list(
    MValue& mvalue)
{
    //auto mlist = new MList;
    auto mlist = diagnostics.new_list();

    mvalue = {T_Array, mlist};
    position current_pos = here();
    MValue m;
    auto mtype = get_item(m);
    while (true) {
        // ',' before the closing bracket is allowed
        if (real_type(mtype)) {
            if (mtype == T_String)
                mlist->emplace_back(ch_buffer);
            else
                mlist->emplace_back(m);
            current_pos = here();
            mtype = get_item(m);
            if (mtype == T_Token_ListEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item(m);
                continue;
            }
            if (real_type(mtype) && mtype != T_String) {
                m.free();
            }
            error(std::string("Reading list, expecting ',' or ']' at position ")
                      .append(pos(current_pos)));
        }
        if (mtype == T_Token_ListEnd)
            break;
        if (mtype == T_Macro) {
            try {
                m = macro_map.get(ch_buffer);
                mtype = m.type;
                continue;
            } catch (const std::out_of_range& e) {
                error(std::string("Undefined macro name at position ").append(pos(current_pos)));
            }
        }
        error(std::string("Expecting list item or ']' at position ").append(pos(current_pos)));
    }
}

MValue* map_search(
    MMap& mmap, std::string_view key)
{
    for (auto& mp : mmap) {
        if (mp.first == key)
            return &mp.second; //TODO: Is this really the address of value?
    }
    return nullptr;
}

void InputBuffer::get_map(
    MValue& mvalue)
{
    //auto mmap = new MMap;
    auto mmap = diagnostics.new_map();

    mvalue = {T_PairArray, mmap};
    position current_pos = here();
    MValue m;
    auto mtype = get_item(m);
    while (true) {
        // ',' before the closing bracket is allowed
        // expect key
        if (mtype == T_String) {
            // Check uniqueness of key
            if (map_search(*mmap, ch_buffer)) {
                error(std::string("Map key has already been defined: ")
                          .append(ch_buffer)
                          .append(" ... current position ")
                          .append(pos(current_pos)));
            }
            std::string s = ch_buffer; // save key

            current_pos = here();
            mtype = get_item(m);
            // expect ':'
            if (mtype != T_Token_Colon) {
                if (real_type(mtype) && mtype != T_String) {
                    m.free();
                }
                error(
                    std::string("Expecting ':' in Map item at position ").append(pos(current_pos)));
            }
            current_pos = here();
            mtype = get_item(m);
            // expect value
            if (mtype == T_Macro) {
                try {
                    // This is duplicating the reference to the
                    // macro value. The FreeMinion deallocator can handle
                    // this by keeping track of deleted nodes.
                    // If a different deallocator should be used,
                    // a deep copy might be better.
                    m = macro_map.get(ch_buffer);
                    mtype = m.type;
                } catch (const std::out_of_range& e) {
                    error(std::string("Expecting map value, undefined macro name at position ")
                              .append(pos(current_pos)));
                }
            } else if (!real_type(mtype)) {
                error(std::string("Expecting map value at position ").append(pos(current_pos)));
            }
            if (mtype == T_String)
                mmap->emplace_back(s, ch_buffer);
            else
                mmap->emplace_back(s, m);
            current_pos = here();
            mtype = get_item(m);
            if (mtype == T_Token_MapEnd) {
                break;
            } else if (mtype == T_Token_Comma) {
                current_pos = here();
                mtype = get_item(m);
                continue;
            }
            error(std::string("Reading map, expecting ',' or '}' at position ")
                      .append(pos(current_pos)));
        } else if (mtype == T_Token_MapEnd) {
            break;
        }
        if (real_type(mtype)) {
            m.free();
        }
        error(std::string("Reading map, expecting a key at position ").append(pos(current_pos)));
    }
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

const char* InputBuffer::read(
    MinionValue& data, std::string_view input_string)
{
    input = input_string;
    ch_index = 0;
    line_index = 0;
    ch_linestart = 0;

    data.free(); //TODO--?
    macro_map.clear();

    try {
        while (true) {
            position current_pos = here();
            short mtype = get_item(data);
            if (mtype == T_Macro) {
                // Check for duplicate
                if (macro_map.has(ch_buffer)) {
                    error(std::string("Position ")
                              .append(pos(current_pos))
                              .append(": macro name already defined"));
                }
                std::string mkey = ch_buffer;
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
                if (real_type(mtype)) {
                    // Add the macro
                    if (mtype == T_String)
                        macro_map.add(mkey, MValue(ch_buffer));
                    else {
                        // Transfer the value to th macro map
                        macro_map.add(mkey, data);
                        data.type = T_NoType;
                        data.minion_item = nullptr;
                    }
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
            if (real_type(mtype)) {
                // found document item
                if (mtype == T_String)
                    data = MValue(ch_buffer);
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

    } catch (MinionError& e) {
        //TODO ... handle this!
        printf("!!! error: %s\n", e.what());
        fflush(stdout);
        data.free();
        macro_map.clear();
        return e.what(); //TODO: Might this be volatile?!
    }

    macro_map.clear();
    return "TODO: Result (error message, if any)";
}

void DumpBuffer::dump_string(
    const std::string& source)
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
    int len = source.size();
    if (len != 0) {
        auto d = depth;
        if (depth >= 0)
            ++depth;
        for (int i = 0; i < len; ++i) {
            dump_pad();
            dump_value(source.at(i));
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
    int len = source.size();
    if (len != 0) {
        auto d = depth;
        if (depth >= 0)
            ++depth;
        for (const auto& mp : source) {
            dump_pad();
            dump_string(mp.first);
            add(':');
            if (depth >= 0)
                add(' ');
            dump_value(mp.second);
            add(',');
        }
        depth = d;
        pop();
        dump_pad();
    }
    add('}');
}

void DumpBuffer::dump_value(
    const MValue& source)
{
    switch (source.type) {
    case T_String:
        dump_string(*reinterpret_cast<MString*>(source.minion_item));
        break;
    case T_Array:
        dump_list(*reinterpret_cast<MList*>(source.minion_item));
        break;
    case T_PairArray:
        dump_map(*reinterpret_cast<MMap*>(source.minion_item));
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

// *** Special MValue "constructors" ***

// Build a new minion string item from the given string view.
MValue::MValue(
    std::string_view s)
    : type{T_String} //, minion_item{new MString(std::string{s})}
    , minion_item{diagnostics.new_string(std::string{s})}
{}

// Build a new minion list item from the given entries, which are of type
// MValue.
MValue::MValue(
    std::initializer_list<MValue> items)
    : type{T_Array} //, minion_item{new MList}
    , minion_item{diagnostics.new_list()}
{
    for (const auto& item : items) {
        reinterpret_cast<MList*>(minion_item)->emplace_back(item);
    }
}

// Build a new minion map from the given entries, which are string/value
// pairs.
MValue::MValue(
    std::initializer_list<MPair> items)
    : type{T_PairArray} //, minion_item{new MMap}
    , minion_item{diagnostics.new_map()}
{
    for (const auto& item : items) {
        reinterpret_cast<MMap*>(minion_item)->emplace_back(item);
    }
}

MString* MValue::m_string()
{
    if (type == T_String)
        return reinterpret_cast<MString*>(minion_item);
    return nullptr;
}

MList* MValue::m_list()
{
    if (type == T_Array)
        return reinterpret_cast<MList*>(minion_item);
    return nullptr;
}

MMap* MValue::m_map()
{
    if (type == T_PairArray)
        return reinterpret_cast<MMap*>(minion_item);
    return nullptr;
}

} // End of namespace minion
