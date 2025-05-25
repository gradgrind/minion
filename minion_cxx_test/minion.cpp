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
 *
 * The `InputBuffer` class manages the structures used during parsing.
 * It allows some of these to be reused when multiple source strings are
 * parsed, to minimize memory allocation and deallocation.
 * 
 * The basic structure used to represent a MINION item is the `MValue`,
 * which is basically a type field (primarily string, list or map) and
 * a pointer to the item's data (which is allocated on the heap). To ease
 * manipulation of these items without having to worry about allocations
 * and deallocations, `MValue` has no destructor for the addressed data.
 * On the other hand, care must be taken not to leak the memory allocated
 * when such a data item is created in the first place. To alleviate this
 * problem to some extent there is also the `MValue` subclass `MinionValue`,
 * which is basically the same as `MValue`, but does have a destructor for
 * all the structures it refers to directly or indirectly (via its contained
 * lists and maps).
 * 
 * The parsing method `read` takes a reference to a `MinionValue` as
 * argument, into which it then places the result of the parsing operation.
 * Thus, the memory of a whole MINION structure is managed by the root
 * `MinionValue`, while reading of – and even writing to – elements of this
 * structure is done via `MValue` items.
 *
 * TODO: The individual data items (`MString`, `MList`, `MMap`) do have
 * destructors, to which the destructor of `MinionValue` can delegate
 * the deallocation.
 * 
 * To avoid memory leaks, especially in the case of parsing errors, all
 * newly allocated items are immediately added "in-place" to the structure
 * belonging to the root `MinionValue`. Thus there should never be any
 * "floating", unowned data.
 */

/* *** Macros ***
 * 
 * Macros use a special flag within `MValue` to allow their data structures
 * to be shared by all references to them. The macros are built in a
 * separate map structure (name string -> `MValue`). When a macro is used
 * in the MINION source, its `MValue` is (shallow) copied to the new place.
 * Immediately after this, the special "not owner" flag is set on the
 * `MValue` in the macro map. When the macro is used again, this flag will
 * be copied to the new place with the `MValue`, so that the destructor
 * will skip this node. Ownership is effectively transferred to the new
 * place.
 * 
 * Should any macros remain unused, their entry in the macro map will not
 * have the "not owner" flag set, so these entries must be deleted
 * separately after the parsing is complete.
 */

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
        m = new MString(*m_string());
        break;
    case T_List:
        m = new MList(*m_list());
        break;
    case T_Map: {
        auto mmap = new MMap;
        m = {T_Map, mmap};
        auto mm = m.m_map();
        size_t len = mm->size();
        for (size_t i = 0; i < len; ++i) {
            MPair& mp0 = mm->get_pair(i);
            mmap->add({mp0.first, {}});
            MValue& mref = mmap->get_pair(i).second;
            mp0.second.mcopy(mref);
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
        delete m_string();
        break;
    case T_List:
        delete m_list();
        break;
    case T_Map:
        delete m_map();
        break;
    }
}

// Read a string as in `int` value, taking an optional context string
// for error reports.
int string2int(
    std::string& s, std::string_view context = "")
{
    int i;
    try {
        size_t index;
        i = std::stoi(s, &index, 0);
        if (index != s.size())
            throw "";
    } catch (std::out_of_range) {
        std::string msg{"Integer out of range: " + s};
        if (!context.empty())
            msg.append("\n  context: ").append(context);
        throw MinionError(msg);
    } catch (...) {
        std::string msg{"Invalid integer: " + s};
        if (!context.empty())
            msg.append("\n  context: ").append(context);
        throw MinionError(msg);
    }
    return i;
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
    std::string_view s)
{
    auto i = macro_map.search(s);
    if (i < 0) {
        error(std::string("Unknown macro name: ")
                  .append(ch_buffer)
                  .append(" ... current position ")
                  .append(pos(here())));
    }
    MValue& mp = macro_map.get_pair(i).second;
    MValue m = mp;
    mp.not_owner = true;
    return m;
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
                    if (macro_map.search(ch_buffer) >= 0) {
                        error(std::string("Macro key has already been defined: ")
                                  .append(ch_buffer)
                                  .append(" ... current position ")
                                  .append(pos(here())));
                    }
                    macro_map.add({ch_buffer, MValue{T_Macro, nullptr}});
                    get_item(macro_map.get_pair(macro_map.size() - 1).second, Expect_Colon);
                    expect = Expect_Comma;
                    continue;

                case T_Macro: // top-level, macro value definition
                    get_bare_string(ch);
                    mvalue = get_macro(ch_buffer);
                    expect = Expect_Comma;
                    continue;

                case T_List: // list value
                    get_bare_string(ch);
                    mvalue.m_list()->add(get_macro(ch_buffer));
                    expect = Expect_Comma;
                    continue;

                case T_Pair: // map value                {
                    get_bare_string(ch);
                    mvalue = get_macro(ch_buffer);
                    return;
                }
            }
            error(std::string("Unexpected macro name at position ").append(pos(here())));
            break; // unreachable

        case '[':
            if (expect == Expect_Value) {
                switch (mvalue.type) {
                case T_NoType: // top-level value
                    mvalue = new MList();
                    get_item(mvalue);
                    // No further input expected
                    expect = Expect_End;
                    continue;

                case T_List: // list value
                {
                    MValue m = new MList();
                    mvalue.m_list()->add(m);
                    get_item(m);
                    expect = Expect_Comma;
                    continue;
                }

                case T_Pair: // map value                {
                {
                    mvalue = new MList();
                    get_item(mvalue);
                    return;
                }

                case T_Macro: // top-level, macro value definition
                {
                    mvalue = new MList();
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
                    mvalue.m_list()->add(m);
                    get_item(m);
                    expect = Expect_Comma;
                    continue;
                }
                case T_Pair: // map value                {
                {
                    mvalue = {T_Map, new MMap()};
                    get_item(mvalue);
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
                    mvalue = new MString(ch_buffer);
                    // No further input expected
                    expect = Expect_End;
                    continue;
                case T_Map: // map-key value
                {
                    get_string(ch);
                    // check that the key is unique
                    auto mm = mvalue.m_map();
                    if (mm->search(ch_buffer) >= 0) {
                        error(std::string("Map key has already been defined: ")
                                  .append(ch_buffer)
                                  .append(" ... current position ")
                                  .append(pos(here())));
                    }
                    mm->add({ch_buffer, {T_Pair, {}}});
                    MValue& m = mm->get_pair(mm->size() - 1).second;
                    get_item(m, Expect_Colon);
                    expect = Expect_Comma;
                    continue;
                }
                case T_List: // list value
                    get_string(ch);
                    mvalue.m_list()->add(new MString{ch_buffer});
                    expect = Expect_Comma;
                    continue;
                case T_Pair: // map value                {
                    get_string(ch);
                    mvalue = new MString(ch_buffer);
                    return;
                case T_Macro:
                    get_string(ch);
                    mvalue = new MString(ch_buffer);
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

const char* InputBuffer::read(
    MinionValue& data, std::string_view input_string)
{
    // Prepare input buffer
    input = input_string;
    ch_index = 0;
    line_index = 0;
    ch_linestart = 0;

    // Clear result data, just to be sure ...
    data = {};
    macro_map.clear();

    try {
        get_item(data);
    } catch (MinionError& e) {
        data = {};
        macro_map.clear();
        error_message = e.what();
        return error_message.c_str();
    } catch (...) {
        data = {};
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
    MString& source)
{
    dump_string(source.data_view());
}

void DumpBuffer::dump_string(
    std::string_view source)
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
            dump_value(source.get(i));
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
        for (int i = 0; i < len; ++i) {
            dump_pad();
            MPair& mp = source.get_pair(i);
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
    MValue& source)
{
    switch (source.type) {
    case T_String:
        dump_string(*source.m_string());
        break;
    case T_List:
        dump_list(*source.m_list());
        break;
    case T_Map:
        dump_map(*source.m_map());
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
        if (pretty != 0)
            indent = pretty;
    }
    buffer.clear();
    dump_value(data);
    return buffer.c_str();
}

// *** Special MValue "constructors" ***

// Build a new minion string item from the given MString*.
MValue::MValue(
    MString* m)
    : type{T_String}
    , minion_item{m}
{}

// Build a new minion list item from the given MList*.
MValue::MValue(
    MList* m)
    : type{T_List}
    , minion_item{m}
{}

// Build a new minion map item from the given MMap*.
MValue::MValue(
    MMap* m)
    : type{T_Map}
    , minion_item{m}
{}

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

bool MMap::get_string(
    std::string_view key, std::string& s)
{
    MValue m = get(key);
    if (m.is_null())
        return false;
    if (MString* ms = m.m_string()) {
        s = ms->data_view();
        return true;
    }
    std::string msg{"Map: value not string for key: "};
    throw MinionError(msg.append(key));
}

bool MMap::get_int(
    std::string_view key, int& i)
{
    std::string s;
    if (!get_string(key, s))
        return false;
    std::string msg{"Map: value not string for key: "};
    i = string2int(s, msg.append(key));
    return true;
}

} // End of namespace minion
