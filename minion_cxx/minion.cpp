#include "minion.h"
#include <map>

namespace minion {

enum minion_type {
    T_NoType = 0,
    T_String,
    T_List,
    T_Map,

    T_Pair,
    T_Macro
};

enum expect_type { Expect_Value = 0, Expect_Comma, Expect_Colon, Expect_End };

const inline std::map<int, std::string> seek_message = {{T_NoType, "top-level value"},
                                                        {T_String, "string"},
                                                        {T_List, "list entry"},
                                                        {T_Map, "map entry key"},
                                                        {T_Pair, "map entry value"},
                                                        {T_Macro, "macro value definition"}};

/* *** Memory management ***
 * TODO ... not up to date ...
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

void MValue::mcopy(
    MValue& m)
{
    switch (type) {
    case T_String:
        m = MValue(T_String, new MString(*reinterpret_cast<MString*>(minion_item)));
        break;
    case T_List: {
        auto mlist = new MList;
        m = {T_List, mlist};
        auto ml = reinterpret_cast<MList*>(minion_item);
        for (auto& v : *ml) {
            mlist->emplace_back(MValue{});
            MValue& mref = (*mlist)[mlist->size() - 1];
            v.mcopy(mref);
        }
        break;
    };
    case T_Map: {
        auto mmap = new MMap;
        m = {T_Map, mmap};
        auto mm = reinterpret_cast<MMap*>(minion_item);
        for (auto& mp : *mm) {
            auto mpair = new MPair(mp->first, {});
            mmap->emplace_back(mpair);
            mp->second.mcopy(mpair->second);
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
    switch (type) {
    case T_String:
        delete reinterpret_cast<MString*>(minion_item);
        break;
    case T_List: {
        auto ml = reinterpret_cast<MList*>(minion_item);
        for (auto& v : *ml) {
            v.free(); // delete the entry value
        }
        delete ml; // delete the vector
    } break;
    case T_Map:
        auto mm = reinterpret_cast<MMap*>(minion_item);
        for (auto& mp : *mm) {
            mp->second.free(); // delete the entry value
            delete mp;         // delete the entry (including the key)
        }
        delete mm; // delete the vector
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

void InputBuffer::get_bare_string(
    char ch)
{
    ch_buffer.clear();
    while (true) {
        ch_buffer.push_back(ch);
        switch (ch = read_ch(false)) {
        case ':':
        case ',':
        case ']':
        case '}':
            unread_ch();
        case ' ':
        case '\n':
        case 0:
            return;
        case '{':
        case '[':
        case '\\':
        case '"': {
            auto s = std::string("Unexpected character ('");
            s.push_back(ch);
            error(s.append("' at position ").append(pos(here())));
        }
        }
    }
}

// The result is available in `ch_buffer`.
void InputBuffer::get_string(
    char ch)
{
    if (ch != '"') {
        // +++ string without delimiters
        get_bare_string(ch);
        return;
    }
    // +++ a delimited string (terminated by '"')
    // Escapes, introduced by '\', are possible. These are an extension
    // of the JSON escapes – see the MINION specification.
    ch_buffer.clear();
    position start_pos = here();
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

MValue InputBuffer::get_macro(
    std::string& s)
{
    auto m = macro_map.get(s);
    if (m.type == T_NoType) {
        error(std::string("Unknown macro name: ")
                  .append(ch_buffer)
                  .append(" ... current position ")
                  .append(pos(here())));
    }
    return m;
}

MValue* map_search(
    MMap* mmap, std::string_view key)
{
    for (auto& mp : *mmap) {
        if (mp->first == key)
            return &mp->second;
    }
    return nullptr;
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
void InputBuffer::get_item(
    MValue& mvalue, int expect)
{
    char ch;
    while (true) {
        switch (ch = read_ch(false)) {
            // Act according to the next input character.

        case 0: // end of input, no next item
            if (expect != Expect_End) {
                error(std::string("Unexpected end of input data while reading ")
                          .append(seek_message.at(mvalue.type)));
            }
            return;

        case ' ':
        case '\n': // continue seeking start of item
            continue;

        case ':':
            if (expect == Expect_Colon) {
                expect = Expect_Value;
                continue;
            }
            error(std::string("Unexpected ':' while reading ").append(seek_message.at(mvalue.type)));
            break; // unreachable

        case ']':
            if (mvalue.type == T_List && (expect == Expect_Value || expect == Expect_Comma)) {
                return;
            }
            error(std::string("Unexpected ']' while reading ").append(seek_message.at(mvalue.type)));
            break; // unreachable

        case '}':
            if (mvalue.type == T_Map && (expect == Expect_Value || expect == Expect_Comma)) {
                return;
            }
            error(std::string("Unexpected ']' while reading ").append(seek_message.at(mvalue.type)));
            break; // unreachable

        case ',':
            if (expect == Expect_Comma) {
                expect = Expect_Value;
                continue;
            }
            error(std::string("Unexpected ',' while reading ").append(seek_message.at(mvalue.type)));
            break; // unreachable

        case '&': // start of macro name
            // valid at top level, as macro value, or as value in
            // lists or maps
            if (expect == Expect_Value) {
                switch (mvalue.type) {
                case T_NoType: // top-level, macro key definition
                    get_bare_string(ch);
                    // check that the key is unique
                    if (macro_map.has(ch_buffer)) {
                        error(std::string("Macro key has already been defined: ")
                                  .append(ch_buffer)
                                  .append(" ... current position ")
                                  .append(pos(here())));
                    }
                    macro_map.add(ch_buffer, MValue{T_Macro, nullptr});
                    get_item(macro_map.first_value(), Expect_Colon);
                    expect = Expect_Comma;
                    continue;

                case T_Macro: // top-level, macro value definition
                    get_bare_string(ch);
                    mvalue = get_macro(ch_buffer);
                    return;

                case T_List: // list value
                    get_bare_string(ch);
                    reinterpret_cast<MList*>(mvalue.minion_item)->emplace_back(get_macro(ch_buffer));
                    expect = Expect_Comma;
                    continue;

                case T_Pair: // map value                {
                    get_bare_string(ch);
                    reinterpret_cast<MPair*>(mvalue.minion_item)->second = MValue(
                        get_macro(ch_buffer));
                    return;
                }
            }
            error(std::string("Unexpected macro name at position ").append(pos(here())));
            break; // unreachable

        case '[':
            if (expect == Expect_Value) {
                switch (mvalue.type) {
                case T_NoType: // top-level value
                    mvalue = {T_List, new MList()};
                    get_item(mvalue);
                    // No further input expected
                    expect = Expect_End;
                    continue;

                case T_List: // list value
                {
                    MValue m = {T_List, new MList()};
                    reinterpret_cast<MList*>(mvalue.minion_item)->emplace_back(m);
                    get_item(m);
                    expect = Expect_Comma;
                    continue;
                }

                case T_Pair: // map value                {
                {
                    MValue m = {T_List, new MList()};
                    reinterpret_cast<MPair*>(mvalue.minion_item)->second = MValue(m);
                    get_item(m);
                    return;
                }

                case T_Macro: // top-level, macro value definition
                {
                    mvalue = {T_List, new MList()};
                    get_item(mvalue);
                    return;
                }
                }
            }
            error(std::string("Unexpected start of list ('[')"));
            break; // unreachable

        case '{':
            if (expect == Expect_Value) {
                switch (mvalue.type) {
                case T_NoType: // top-level value
                    mvalue = {T_Map, new MMap()};
                    get_item(mvalue);
                    // No further input expected
                    expect = Expect_End;
                    continue;
                case T_List: // list value
                {
                    MValue m = {T_Map, new MMap()};
                    reinterpret_cast<MList*>(mvalue.minion_item)->emplace_back(m);
                    get_item(m);
                    expect = Expect_Comma;
                    continue;
                }
                case T_Pair: // map value                {
                {
                    MValue m = {T_Map, new MMap()};
                    reinterpret_cast<MPair*>(mvalue.minion_item)->second = MValue(m);
                    get_item(m);
                    return;
                }

                case T_Macro: // top-level, macro value definition
                {
                    mvalue = {T_Map, new MMap()};
                    get_item(mvalue);
                    return;
                }
                }
            }
            error(std::string("Unexpected start of map ('{')"));
            break; // unreachable

        case '#': // start comment
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

        case '"': // delimited string
        default:  // start of undelimited string
            if (expect == Expect_Value) {
                switch (mvalue.type) {
                case T_NoType: // top-level value
                    get_string(ch);
                    mvalue = MValue(ch_buffer);
                    // No further input expected
                    expect = Expect_End;
                    continue;
                case T_Map: // map-key value
                {
                    get_string(ch);
                    // check that the key is unique
                    auto mm = reinterpret_cast<MMap*>(mvalue.minion_item);
                    if (map_search(mm, ch_buffer)) {
                        error(std::string("Map key has already been defined: ")
                                  .append(ch_buffer)
                                  .append(" ... current position ")
                                  .append(pos(here())));
                    }
                    auto mp = new MPair(ch_buffer, {});
                    mm->emplace_back(mp);
                    MValue m = {T_Pair, mp};
                    get_item(m, Expect_Colon);
                    expect = Expect_Comma;
                    continue;
                }
                case T_List: // list value
                    get_string(ch);
                    reinterpret_cast<MList*>(mvalue.minion_item)->emplace_back(ch_buffer);
                    expect = Expect_Comma;
                    continue;
                case T_Pair: // map value                {
                    get_string(ch);
                    reinterpret_cast<MPair*>(mvalue.minion_item)->second = MValue(ch_buffer);
                    return;
                case T_Macro:
                    get_string(ch);
                    mvalue = MValue(ch_buffer);
                    return;
                }
            }
            error(std::string("Unexpected start of string value"));
        } // End of character switch
    } // End of item-seeking loop
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
    // Prepare input buffer
    input = input_string;
    ch_index = 0;
    line_index = 0;
    ch_linestart = 0;

    // Clear result data, just to be sure ...
    data.free();
    macro_map.clear();

    try {
        get_item(data);
    } catch (MinionError& e) {
        data.free();
        macro_map.clear();
        error_message = e.what();
        return error_message.c_str();
    } catch (...) {
        data.free();
        macro_map.clear();
        throw;
    }

    /*
    DumpBuffer d;
    for (auto& mp : macro_map.macros) {
        printf("&: %s\n", mp.first.c_str());
        printf("  == %s\n\n", d.dump(mp.second));
    }
    */

    macro_map.clear();
    return nullptr;
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
            dump_string(mp->first);
            add(':');
            if (depth >= 0)
                add(' ');
            dump_value(mp->second);
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
    case T_List:
        dump_list(*reinterpret_cast<MList*>(source.minion_item));
        break;
    case T_Map:
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
    , minion_item{new MString(std::string{s})}
{}

// Build a new minion list item from the given entries, which are of type
// MValue.
MValue::MValue(
    std::initializer_list<MValue> items)
    : type{T_List} //, minion_item{new MList}
    , minion_item{new MList()}
{
    for (const auto& item : items) {
        reinterpret_cast<MList*>(minion_item)->emplace_back(item);
    }
}

// Build a new minion map from the given entries, which are string/value
// pairs.
MValue::MValue(
    std::initializer_list<MPair> items)
    : type{T_Map} //, minion_item{new MMap}
    , minion_item{new MMap()}
{
    for (const auto& item : items) {
        reinterpret_cast<MMap*>(minion_item)->emplace_back(new MPair{item});
    }
}

bool MValue::is_null()
{
    return type == T_NoType;
}

MString* MValue::m_string()
{
    if (type == T_String)
        return reinterpret_cast<MString*>(minion_item);
    return nullptr;
}

MList* MValue::m_list()
{
    if (type == T_List)
        return reinterpret_cast<MList*>(minion_item);
    return nullptr;
}

MMap* MValue::m_map()
{
    if (type == T_Map)
        return reinterpret_cast<MMap*>(minion_item);
    return nullptr;
}

} // End of namespace minion
