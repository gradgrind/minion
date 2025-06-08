package gominion

import (
	"fmt"
	"strconv"
	"strings"
)

const (
	token_End = iota
	token_StartList
	token_EndList
	token_StartMap
	token_EndMap
	token_Comma
	token_Colon
	token_Macro
	token_String
)

var token_text_map = map[int]string{
	token_End:       "end of data",
	token_StartList: "'['",
	token_EndList:   "']'",
	token_StartMap:  "'{'",
	token_EndMap:    "'}'",
	token_Comma:     "','",
	token_Colon:     "':'",
}

type MValue interface {
	Size() int
}

type MString string

func (m MString) Size() int {
	return len(m)
}

type MError string

func (m MError) Size() int {
	return len(m)
}

type MList []MValue

func (m MList) Size() int {
	return len(m)
}

func (m MList) Get(index int) MValue {
	if index >= 0 && index <= len(m) {
		return m[index]
	}
	return nil
}

func (m MList) GetString(index int, s *string) bool {
	v := m.Get(index)
	if v != nil {
		snew, ok := v.(MString)
		if ok {
			*s = string(snew)
			return true
		}
		panic(fmt.Sprintf("List: expecting string at index: %d", index))
	}
	return false
}

func (m MList) GetInt(index int, i *int) bool {
	v := m.Get(index)
	if v != nil {
		snew, ok := m[index].(MString)
		if ok {
			i64, err := strconv.ParseInt(string(snew), 0, 0)
			if err != nil {
				panic(fmt.Sprintf(
					"List: invalid integer (%s) at index %d", snew, index))
			}
			*i = int(i64)
			return true
		}
		panic(fmt.Sprintf("List: expecting integer at index: %d", index))
	}
	return false
}

type MPair struct {
	Key   string
	Value MValue
}

type MMap []MPair

func (m MMap) Size() int {
	return len(m)
}

func (m MMap) Get(key string) MValue {
	for _, p := range m {
		if p.Key == key {
			return p.Value
		}
	}
	return nil
}

func (m MMap) GetString(key string, s *string) bool {
	v := m.Get(key)
	if v != nil {
		snew, ok := v.(MString)
		if ok {
			*s = string(snew)
			return true
		}
		panic(fmt.Sprintf("Map: expecting string value for key: %s", key))
	}
	return false
}

func (m MMap) GetInt(key string, i *int) bool {
	v := m.Get(key)
	if v != nil {
		snew, ok := v.(MString)
		if ok {
			i64, err := strconv.ParseInt(string(snew), 0, 0)
			if err != nil {
				panic(fmt.Sprintf(
					"Map: invalid integer value (%s) for key %s", snew, key))
			}
			*i = int(i64)
			return true
		}
		panic(fmt.Sprintf("Map: expecting integer value for key %s", key))
	}
	return false
}

// Used for recording read-position in input text
type position struct {
	line int
	char int
}

type input_buffer struct {
	macro_map MMap

	// for character-by-character reading
	ch_input     string // input string
	ch_index     int
	ch_linestart int
	line_index   int

	// ch_buffer is used for constructinging strings before they are passed
	// to minion_value items.
	ch_buffer []byte //= make([]byte, 0, 100)
}

func (ib *input_buffer) token_text(token int) string {
	if token == token_String || token == token_Macro {
		return "\"" + string(ib.ch_buffer) + "\""
	}
	return token_text_map[token]
}

func (ib *input_buffer) get_macro(name string) MValue {
	m := ib.macro_map.Get(name)
	if m == nil {
		ib.error(fmt.Sprintf(
			"Unknown macro name: %s\n ... current position %s",
			name,
			pos(ib.here())))
	}
	return m
}

/* Read the next lexical "token" from the input.
 * If it is a string or a macro name, the actual string will be available
 * in `ch_buffer`.
 * If the input is invalid, the function "panics", passing a message.
 */
func (ib *input_buffer) get_token() int {
	var ch byte
	for {
		ch = ib.read_ch(false)
		switch ch {
		// Act according to the next input character.
		case 0: // end of input, no next item
			return token_End
		case ' ':
		case '\n': // continue seeking start of item
			continue
		case ':':
			return token_Colon
		case ',':
			return token_Comma
		case '[':
			return token_StartList
		case ']':
			return token_EndList
		case '{':
			return token_StartMap
		case '}':
			return token_EndMap
		case '"':
			ib.get_string()
			return token_String
		case '&': // start of macro name
			ib.get_bare_string(ch)
			return token_Macro
		case '#': // start comment
			ch = ib.read_ch(false)
			if ch == '[' {
				// Extended comment: read to "]#"
				comment_pos := ib.here()
				ch = ib.read_ch(false)
				for {
					if ch == ']' {
						ch = ib.read_ch(false)
						if ch == '#' {
							break
						}
						continue
					}
					if ch == 0 {
						ib.error(fmt.Sprintf("Unterminated comment ('\\[ ...') at position %s",
							pos(comment_pos)))
					}
					// Comment loop ... read next character
					ch = ib.read_ch(false)
				}
				// End of extended comment
			} else {
				// "Normal" comment: read to end of line
				for {
					if ch == '\n' || ch == 0 {
						break
					}
					ch = ib.read_ch(false)
				}
			}
			continue // continue seeking item
		default:
			ib.get_bare_string(ch)
			return token_String
		}
	}
}

func (ib *input_buffer) read_ch(instring bool) byte {
	if ib.ch_index >= len(ib.ch_input) {
		return 0
	}
	ch := ib.ch_input[ib.ch_index]
	ib.ch_index++
	if ch == '\n' {
		ib.line_index++
		ib.ch_linestart = ib.ch_index
		// these are not acceptable within delimited strings
		if !instring {
			// separator
			return ch
		}
		ib.error(fmt.Sprintf("Unexpected newline in delimited string, line %d",
			ib.line_index))
	} else if ch == '\r' || ch == '\t' {
		// these are acceptable in the source, but not within strings.
		if !instring {
			// separator
			return ' '
		}
	} else if ch >= 32 && ch != 127 {
		return ch
	}
	ib.error(fmt.Sprintf("Illegal character (0x%2x) at position %s",
		ch, pos(ib.here())))
	return 0 // unreachable
}

func (ib *input_buffer) unread_ch() {
	if ib.ch_index == 0 {
		panic("[BUG] unread_ch reached start of data")
	}
	ib.ch_index--
	//NOTE: '\n' is never unread!
}

func (ib *input_buffer) here() position {
	return position{ib.line_index + 1, ib.ch_index - ib.ch_linestart}
}

func pos(p position) string {
	return fmt.Sprintf("%d.%d", p.line, p.char)
}

/* Read a delimited string (terminated by '"') from the input.
 *
 * It is entered after the initial '"' has been read, so the next character
 * will be the first of the string.
 *
 * Escapes, introduced by '\', are possible. These are an extension of the
 * JSON escapes – see the MINION specification.
 *
 * The result is available in `ch_buffer`.
 */
func (ib *input_buffer) get_string() {
	ib.ch_buffer = ib.ch_buffer[:0]
	start_pos := ib.here()
	var ch byte
	for {
		ch = ib.read_ch(true)
		if ch == '"' {
			break
		}
		if ch == 0 {
			ib.error(fmt.Sprintf(
				"End of data reached inside delimited string from position %s",
				pos(start_pos)))
		}
		if ch == '\\' {
			ch = ib.read_ch(false) // '\n' etc. are permitted here
			switch ch {
			case '"', '\\', '/':
				//break
			case 'n':
				ch = '\n'
				//break
			case 't':
				ch = '\t'
				//break
			case 'b':
				ch = '\b'
				//break
			case 'f':
				ch = '\f'
				//break
			case 'r':
				ch = '\r'
				//break
			case 'u':
				ib.add_unicode_to_ch_buffer(4)
				continue
			case 'U':
				ib.add_unicode_to_ch_buffer(6)
				continue
			case '[':
				// embedded comment, read to "\]"
				{
					comment_pos := ib.here()
					ch = ib.read_ch(false)
					for {
						if ch == '\\' {
							ch = ib.read_ch(false)
							if ch == ']' {
								break
							}
							continue
						}
						if ch == 0 {
							ib.error(fmt.Sprintf(
								"End of data reached inside string comment from position %s",
								pos(comment_pos)))
						}
						// loop with next character
						ch = ib.read_ch(false)
					}
				}
				continue // comment ended, seek next character
			default:
				ib.error(fmt.Sprintf(
					"Illegal string escape at position %s",
					pos(ib.here())))
			}
		}
		ib.ch_buffer = append(ib.ch_buffer, ch)
	}
}

// This version reads a non-delimited string.
// The result is available in `ch_buffer`.
func (ib *input_buffer) get_bare_string(ch byte) {
	ib.ch_buffer = ib.ch_buffer[:0]
	for {
		ib.ch_buffer = append(ib.ch_buffer, ch)
		ch = ib.read_ch(false)
		switch ch {
		case ':', ',', ']', '}':
			ib.unread_ch()
			return
		case ' ', '\n', 0:
			return
		case '{', '[', '\\', '"':
			ib.error(fmt.Sprintf(
				"Unexpected character ('%c') at position %s",
				ch,
				pos(ib.here())))
		}
	}
}

func (ib *input_buffer) last_n_chars(n int) string {
	ch_start := 0
	recent := ib.ch_index
	if recent > n {
		ch_start = ib.ch_index - n
		// Find start of utf-8 sequence
		for {
			ch := ib.ch_input[ch_start]
			if ch < 0x80 || (ch >= 0xC0 && ch < 0xF8) {
				break
			}
			ch_start++
		}
	}
	return ib.ch_input[ch_start:ib.ch_index]
}

func (ib *input_buffer) error(msg string) {
	// Add most recently read characters
	panic(msg + "\n ... " + ib.last_n_chars(80))
}

func (ib *input_buffer) get_list() MValue {
	var mlist MList
	for {
		t := ib.get_token()
		switch t {
		case token_EndList:
			return mlist
		case token_String:
			mlist = append(mlist, MString(ib.ch_buffer))
			//break
		case token_StartList:
			mlist = append(mlist, ib.get_list())
			//break
		case token_StartMap:
			mlist = append(mlist, ib.get_map())
			//break
		case token_Macro:
			mlist = append(mlist, ib.get_macro(string(ib.ch_buffer)))
			//break
		default:
			ib.error(fmt.Sprintf(
				"Unexpected item whilst seeking list element: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
		}
		t = ib.get_token()
		if t == token_Comma {
			continue
		}
		if t == token_EndList {
			return mlist
		}
		ib.error(fmt.Sprintf(
			"Unexpected item whilst seeking comma in list: %s\n ... current position %s",
			ib.token_text(t), pos(ib.here())))
	}
}

func (ib *input_buffer) get_map() MValue {
	var mmap MMap
	var key string
	for {
		t := ib.get_token()
		if t != token_String {
			if t == token_EndMap {
				return mmap
			}
			ib.error(fmt.Sprintf(
				"Unexpected item whilst seeking map element key: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
		}
		key = string(ib.ch_buffer)
		t = ib.get_token()
		if t != token_Colon {
			ib.error(fmt.Sprintf(
				"Unexpected item whilst seeking map element colon: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
		}
		t = ib.get_token()
		switch t {
		case token_String:
			mmap = append(mmap, MPair{key, MString(ib.ch_buffer)})
			//break
		case token_StartList:
			mmap = append(mmap, MPair{key, ib.get_list()})
			//break
		case token_StartMap:
			mmap = append(mmap, MPair{key, ib.get_map()})
			//break
		case token_Macro:
			mmap = append(mmap, MPair{key, ib.get_macro(string(ib.ch_buffer))})
			//break
		default:
			ib.error(fmt.Sprintf(
				"Unexpected item whilst seeking map element value: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
		}
		t = ib.get_token()
		if t == token_Comma {
			continue
		}
		if t == token_EndMap {
			return mmap
		}
		ib.error(fmt.Sprintf(
			"Unexpected item whilst seeking comma in map: %s\n ... current position %s",
			ib.token_text(t), pos(ib.here())))
	}
}

// Convert a unicode code point (as hex string) to a UTF-8 string
func (ib *input_buffer) add_unicode_to_ch_buffer(l int) {
	buf := make([]byte, l)
	for i := range l {
		buf[i] = ib.read_ch(true)
	}
	number, err := strconv.ParseUint(string(buf), 16, 0) // Parse as base 16
	if err == nil {
		s := string(rune(number))
		// Check validity (if invalid, the string will be "\uFFFD")
		if s != "\uFFFD" {
			// Add new character to read_buffer
			ib.ch_buffer = append(ib.ch_buffer, s...)
			return
		}
	}
	ib.error(fmt.Sprintf(
		"Invalid Unicode escape in string, position %s",
		pos(ib.here())))
}

func ReadMinion(input_string string) (val MValue) {
	// Prepare input buffer
	ib := input_buffer{
		ch_input:     input_string,
		ch_index:     0,
		line_index:   0,
		ch_linestart: 0,
	}

	// Catch errors (panic calls)
	defer func() {
		//clear(ib.macro_map)
		if r := recover(); r != nil {
			e, ok := r.(string)
			if ok {
				val = MError(e)
			} else {
				panic(r)
			}
		}
	}()

	val = ib.read_val()
	return val
}

func (ib *input_buffer) read_val() MValue {
	var m MValue
	var key string
	for {
		t := ib.get_token()
		switch t {
		case token_String:
			m = MString(ib.ch_buffer)
			//break
		case token_StartList:
			m = ib.get_list()
			//break
		case token_StartMap:
			m = ib.get_map()
			//break
		case token_Macro:
			key = string(ib.ch_buffer)
			t = ib.get_token()
			if t != token_Colon {
				ib.error(fmt.Sprintf(
					"Unexpected item whilst seeking macro definition colon: %s\n ... current position %s",
					ib.token_text(t), pos(ib.here())))
			}
			t = ib.get_token()
			switch t {
			case token_String:
				ib.macro_map = append(ib.macro_map, MPair{key, MString(ib.ch_buffer)})
				//break
			case token_StartList:
				ib.macro_map = append(ib.macro_map, MPair{key, ib.get_list()})
				//break
			case token_StartMap:
				ib.macro_map = append(ib.macro_map, MPair{key, ib.get_map()})
				//break
			case token_Macro:
				ib.macro_map = append(ib.macro_map, MPair{key, ib.get_macro(string(ib.ch_buffer))})
				//break
			default:
				ib.error(fmt.Sprintf(
					"Unexpected item whilst seeking macro definition value: %s\n ... current position %s",
					ib.token_text(t), pos(ib.here())))
			}
			t = ib.get_token()
			if t == token_Comma {
				continue
			}
			ib.error(fmt.Sprintf(
				"Expecting comma after macro definition, unexpected item: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
			//break
		default:
			ib.error(fmt.Sprintf(
				"Unexpected item whilst seeking top-level element: %s\n ... current position %s",
				ib.token_text(t), pos(ib.here())))
		}
		t = ib.get_token()
		if t == token_End {
			break
		}
		ib.error(fmt.Sprintf(
			"Expecting end of data, unexpected item: %s\n ... current position %s",
			ib.token_text(t), pos(ib.here())))
	}
	return m
}

// *******************************
// *** dump functions (serializing)

type dump_buffer struct {
	indent int // = 2;
	depth  int
	buffer strings.Builder
}

func DumpString(source string) string {
	buffer := strings.Builder{}
	buffer.WriteByte('"')
	var ch byte
	for _, ch = range []byte(source) {
		switch ch {
		case '"':
			buffer.WriteString("\\\"")
			//break
		case '\n':
			buffer.WriteString("\\n")
			//break
		case '\t':
			buffer.WriteString("\\t")
			//break
		case '\b':
			buffer.WriteString("\\b")
			//break
		case '\f':
			buffer.WriteString("\\f")
			//break
		case '\r':
			buffer.WriteString("\\r")
			//break
		case '\\':
			buffer.WriteString("\\\\")
			//break
		case 127:
			buffer.WriteString("\\u007F")
			//break
		default:
			if ch >= 32 {
				buffer.WriteByte(ch)
				continue
			}
			buffer.WriteString("\\u00")
			if ch >= 16 {
				buffer.WriteByte('1')
				ch -= 16
			} else {
				buffer.WriteByte('0')
			}
			if ch >= 10 {
				buffer.WriteByte('A' + ch - 10)
			} else {
				buffer.WriteByte('0' + ch)
			}
		}
	}
	buffer.WriteByte('"')
	return buffer.String()
}

func (db *dump_buffer) dump_string(source string) {
	db.buffer.WriteString(DumpString(source))
}

func (db *dump_buffer) dump_pad() {
	if db.depth >= 0 {
		db.buffer.WriteByte('\n')
		if db.depth > 0 {
			db.buffer.WriteString(
				strings.Repeat(" ", db.depth*db.indent))
		}
	}
}

func (db *dump_buffer) dump_list(source MList) {
	db.buffer.WriteByte('[')
	if source.Size() != 0 {
		var d = db.depth
		if d >= 0 {
			db.depth++
		}
		n := len(source)
		for _, m := range source {
			db.dump_pad()
			db.dump_value(m)
			n--
			if n != 0 {
				db.buffer.WriteByte(',')
			}
		}
		db.depth = d
		db.dump_pad()
	}
	db.buffer.WriteByte(']')
}

func (db *dump_buffer) dump_map(source MMap) {
	db.buffer.WriteByte('{')
	if source.Size() != 0 {
		var d = db.depth
		if d >= 0 {
			db.depth++
		}
		n := len(source)
		for _, m := range source {
			db.dump_pad()
			db.dump_string(m.Key)
			db.buffer.WriteByte(':')
			if d >= 0 {
				db.buffer.WriteByte(' ')
			}
			db.dump_value(m.Value)
			n--
			if n != 0 {
				db.buffer.WriteByte(',')
			}
		}
		db.depth = d
		db.dump_pad()
	}
	db.buffer.WriteByte('}')
}

func (db *dump_buffer) dump_value(source MValue) {
	switch source := source.(type) {
	case MString:
		// Strings don't receive any extra formatting
		db.dump_string(string(source))
		//break
	case MList:
		db.dump_list(source)
		//break
	case MMap:
		db.dump_map(source)
		//break
	default:
		panic(fmt.Sprintf("[BUG] MINION dump: bad MValue type: %#v", source))
	}
}

func DumpMinion(source MValue, pretty int) string {
	// Get a new dump buffer
	var dbuf = dump_buffer{indent: 2, depth: -1}
	if pretty >= 0 {
		dbuf.depth = 0
		if pretty != 0 {
			dbuf.indent = pretty
		}
	}
	dbuf.dump_value(source)
	return string(dbuf.buffer.String())
}
