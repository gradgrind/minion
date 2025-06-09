#include "minion.h"
#include <map>

namespace minion {

enum tokens {
    Token_End = 0,
    Token_StartList,
    Token_EndList,
    Token_StartMap,
    Token_EndMap,
    Token_Comma,
    Token_Colon,
    Token_Macro,
    Token_String
};

inline const std::map<int, std::string> token_text_map{{Token_End, "end of data"},
                                                       {Token_StartList, "'['"},
                                                       {Token_EndList, "']'"},
                                                       {Token_StartMap, "'{'"},
                                                       {Token_EndMap, "'}'"},
                                                       {Token_Comma, "','"},
                                                       {Token_Colon, "':'"}};

std::string Reader::token_text(
    int token)
{
    if (token == Token_String || token == Token_Macro)
        return "\"" + ch_buffer + "\"";
    return token_text_map.at(token);
}

// Read a string as an `int` value, taking an optional context string
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
    } catch (std::out_of_range& e) {
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

char Reader::read_ch(
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

void Reader::unread_ch()
{
    if (ch_index == 0) {
        error("[BUG] unread_ch reached start of data");
    }
    --ch_index;
    //NOTE: '\n' is never unread!
}

void Reader::error(
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

// The result is available in `ch_buffer`.
void Reader::get_bare_string(
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
void Reader::get_string()
{
    // +++ a delimited string (terminated by '"')
    // Escapes, introduced by '\', are possible. These are an extension
    // of the JSON escapes – see the MINION specification.
    char ch;
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

MValue Reader::get_macro(
    std::string_view s)
{
    auto m = macro_map.get(s);
    if (m.is_null())
        error(std::string("Unknown macro name: ")
                  .append(s)
                  .append(" ... current position ")
                  .append(pos(here())));
    return m;
}

/* Read the next lexical "token" from the input.
 * If it is a string or a macro name, the actual string will be available
 * in `ch_buffer`.
 * If the input is invalid, a MinionError exception will be thrown,
 * containing a message.
 */
int Reader::get_token()
{
    char ch;
    while (true) {
        switch (ch = read_ch(false)) {
            // Act according to the next input character.
        case 0: // end of input, no next item
            return Token_End;
        case ' ':
        case '\n': // continue seeking start of item
            continue;
        case ':':
            return Token_Colon;
        case ',':
            return Token_Comma;
        case '[':
            return Token_StartList;
        case ']':
            return Token_EndList;
        case '{':
            return Token_StartMap;
        case '}':
            return Token_EndMap;
        case '"':
            get_string();
            return Token_String;
        case '&': // start of macro name
            get_bare_string(ch);
            return Token_Macro;
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
        default:
            get_bare_string(ch);
            return Token_String;
        }
    }
}

MValue Reader::get_list()
{
    MList mlist;
    while (true) {
        auto t = get_token();
        switch (t) {
        case Token_EndList:
            return mlist;
        case Token_String:
            mlist.emplace_back(ch_buffer);
            break;
        case Token_StartList:
            mlist.emplace_back(get_list());
            break;
        case Token_StartMap:
            mlist.emplace_back(get_map());
            break;
        case Token_Macro:
            mlist.emplace_back(get_macro(ch_buffer));
            break;
        default:
            error(std::string("Unexpected item whilst seeking list element: ")
                      .append(token_text(t))
                      .append(" ... current position ")
                      .append(pos(here())));
        }
        t = get_token();
        if (t == Token_Comma)
            continue;
        if (t == Token_EndList)
            return mlist;
        error(std::string("Unexpected item whilst seeking comma in list: ")
                  .append(token_text(t))
                  .append(" ... current position ")
                  .append(pos(here())));
    }
}

MValue Reader::get_map()
{
    MMap mmap;
    std::string key;
    while (true) {
        auto t = get_token();
        if (t != Token_String) {
            if (t == Token_EndMap)
                return mmap;
            error(std::string("Unexpected item whilst seeking map element key: ")
                      .append(token_text(t))
                      .append(" ... current position ")
                      .append(pos(here())));
        }
        key = ch_buffer;
        t = get_token();
        if (t != Token_Colon) {
            error(std::string("Unexpected item whilst seeking map element colon: ")
                      .append(token_text(t))
                      .append(" ... current position ")
                      .append(pos(here())));
        }
        switch (t = get_token()) {
        case Token_String:
            mmap.emplace_back(key, ch_buffer);
            break;
        case Token_StartList:
            mmap.emplace_back(key, get_list());
            break;
        case Token_StartMap:
            mmap.emplace_back(key, get_map());
            break;
        case Token_Macro:
            mmap.emplace_back(key, get_macro(ch_buffer));
            break;
        default:
            error(std::string("Unexpected item whilst seeking map element value: ")
                      .append(token_text(t))
                      .append(" ... current position ")
                      .append(pos(here())));
        }
        t = get_token();
        if (t == Token_Comma)
            continue;
        if (t == Token_EndMap)
            return mmap;
        error(std::string("Unexpected item whilst seeking comma in map: ")
                  .append(token_text(t))
                  .append(" ... current position ")
                  .append(pos(here())));
    }
}

// Convert a unicode code point (as hex string) to a UTF-8 string
bool Reader::add_unicode_to_ch_buffer(
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

//static
MValue Reader::read(
    std::string_view s)
{
    return Reader(s).result;
}

Reader::Reader(
    std::string_view input_string)
    : input{input_string}
    , ch_index{0}
    , line_index{0}
    , ch_linestart{0}
{
    std::string key;
    try {
        while (true) {
            auto t = get_token();
            switch (t) {
            case Token_String:
                result = ch_buffer;
                break;
            case Token_StartList:
                result = get_list();
                break;
            case Token_StartMap:
                result = get_map();
                break;
            case Token_Macro:
                key = ch_buffer;
                t = get_token();
                if (t != Token_Colon) {
                    error(std::string("Unexpected item whilst seeking macro definition colon: ")
                              .append(token_text(t))
                              .append(" ... current position ")
                              .append(pos(here())));
                }
                switch (t = get_token()) {
                case Token_String:
                    macro_map.emplace_back(key, ch_buffer);
                    break;
                case Token_StartList:
                    macro_map.emplace_back(key, get_list());
                    break;
                case Token_StartMap:
                    macro_map.emplace_back(key, get_map());
                    break;
                case Token_Macro:
                    macro_map.emplace_back(key, get_macro(ch_buffer));
                    break;
                default:
                    error(std::string("Unexpected item whilst seeking macro definition value: ")
                              .append(token_text(t))
                              .append(" ... current position ")
                              .append(pos(here())));
                }
                t = get_token();
                if (t == Token_Comma)
                    continue;
                error(std::string("Expecting comma after macro definition, unexpected item: ")
                          .append(token_text(t))
                          .append(" ... current position ")
                          .append(pos(here())));
                break; // unreachable
            default:
                error(std::string("Unexpected item whilst seeking top-level element: ")
                          .append(token_text(t))
                          .append(" ... current position ")
                          .append(pos(here())));
            }
            t = get_token();
            if (t == Token_End)
                break;
            error(std::string("Expecting end of data, unexpected item: ")
                      .append(token_text(t))
                      .append(" ... current position ")
                      .append(pos(here())));
        }

    } catch (MinionError& e) {
        result = e;
    }
}

//static method
std::string Writer::dumpString(
    std::string_view source)
{
    auto w = Writer();
    w.dump_string(source);
    return w.buffer;
}

void Writer::dump_string(
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

void Writer::dump_pad()
{
    if (depth >= 0) {
        add('\n');
        for (int i = 0; i < depth * indent; ++i)
            add(' ');
    }
}

void Writer::dump_list(
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

void Writer::dump_map(
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

void Writer::dump_value(
    MValue& source)
{
    switch (source.type()) {
    case T_String:
        dump_string(**source.m_string());
        break;
    case T_List:
        dump_list(**source.m_list());
        break;
    case T_Map:
        dump_map(**source.m_map());
        break;
    default:
        throw "[BUG] MINION dump: bad MValue type: " + std::to_string(source.type());
    }
}

std::string_view Writer::dump()
{
    return buffer;
}

const char* Writer::dump_c()
{
    return buffer.c_str();
}

Writer::Writer(
    MValue& data, int pretty)
{
    depth = -1;
    if (pretty >= 0) {
        depth = 0;
        if (pretty != 0)
            indent = pretty;
    }
    dump_value(data);
}

bool MList::get_string(
    size_t index, std::string& s)
{
    if (index < size()) {
        MValue m = get(index);
        if (auto ms = m.m_string()) {
            s = **ms;
            return true;
        }
        std::string msg{"List: expecting string at index: "};
        throw MinionError(msg.append(std::to_string(index)));
    }
    return false; // out of range
}

bool MList::get_int(
    size_t index, int& i)
{
    std::string s;
    if (!get_string(index, s))
        return false; // out of range
    std::string msg{"List, seeking integer value at index: "};
    i = string2int(s, msg.append(std::to_string(index)));
    return true;
}

MValue MMap::get(
    std::string_view key)
{
    for (auto& mp : *this) {
        if (mp.first == key)
            return mp.second;
    }
    return {};
}

bool MMap::get_string(
    std::string_view key, std::string& s)
{
    MValue m = get(key);
    if (m.is_null())
        return false;
    if (auto ms = m.m_string()) {
        s = **ms;
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
    std::string msg{"Map, seeking integer value at key: "};
    i = string2int(s, msg.append(key));
    return true;
}

MValue::MValue(
    std::initializer_list<MValue> items)
    : _MV{std::make_shared<MList>()}
{
    auto p = std::get_if<std::shared_ptr<MList>>(this)->get();
    for (const auto& item : items) {
        p->emplace_back(item);
    }
}

} // End of namespace minion
