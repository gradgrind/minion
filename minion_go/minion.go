package minion

import (
	"fmt"
	"slices"
	"strconv"
)

type FlagType uint16

type MinionString string

func (m MinionString) Size() int {
	return len(m)
}

type MinionArray []MinionValue

func (m MinionArray) Size() int {
	return len(m)
}

type MinionPair struct {
	Key   MinionString
	Value MinionValue
}

type MinionPairArray []MinionPair

func (m MinionPairArray) Size() int {
	return len(m)
}

type MinionData interface {
	Size() int
}

type MinionValue struct {
	Type  FlagType
	Flags FlagType
	Data  MinionData
}

type MinionMacros map[MinionString]MinionValue

type MinionDoc struct {
	Item   MinionValue
	Error  string
	Macros MinionMacros
}

const MIN_FLAG = 8
const (
	F_Simple_String = iota + MIN_FLAG // undelimited string
	F_Error
	F_Macro
	F_Token_End
	F_Token_String_Delim
	F_Token_ListStart
	F_Token_ListEnd
	F_Token_MapStart
	F_Token_MapEnd
	F_Token_Comma
	F_Token_Colon

	F_NoFlags = 0
	// This bit will be set if the data field refers to memory that this
	// item does not "own", i.e. it shouldn't be freed.
	F_MACRO_VALUE = 32
)

const (
	T_NoType = iota
	T_String
	T_Array
	T_PairArray
)

var (
	// for character-by-character reading
	ch_input     string // input string
	ch_index     int    = 0
	ch_linestart int    = 0
	line_index          = 0

	// read_buffer is used for constructinging strings before they are passed
	// to minion_value items.
	read_buffer []byte = make([]byte, 0, 100)

	// dump_buffer is used for serializing a minion_value.
	dump_buffer []byte = make([]byte, 0, 100)
	indentation int    = 2 // pretty-print indentation

	// Keep track of "unbound" minion items.
	// The buffer for these items is used as a stack.
	remembered_items []MinionValue = make([]MinionValue, 0, 10)

	// Keep track of macros
	macros MinionMacros
)

type position struct {
	line int
	char int
}

func read_ch(instring bool) byte {
	if ch_index == len(ch_input) {
		return 0
	}
	ch := ch_input[ch_index]
	ch_index++
	if ch == '\n' {
		line_index++
		ch_linestart = ch_index
		// these are not acceptable within delimited strings
		if !instring {
			// separator
			return ch
		}
		panic(fmt.Sprintf("Unexpected newline in delimited string, line %d", line_index))
	} else if ch == '\r' || ch == '\t' {
		// these are acceptable in the source, but not within strings.
		if !instring {
			// separator
			return ' '
		}
	} else if ch >= 32 && ch != 127 {
		return ch
	}
	panic(fmt.Sprintf("Illegal character (0x%2x) at position %s", ch, pos(here())))
}

func here() position {
	return position{line_index + 1, ch_index - ch_linestart}
}

func pos(p position) string {
	return fmt.Sprintf("%d.%d", p.line, p.char)
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

func get_item() FlagType {
	var ch byte // for character-by-character reading
	// Set read_buffer to "empty", keeping the underlying memory:
	var result FlagType
	read_buffer = read_buffer[:0]

	for {
		ch = read_ch(false)
		if len(read_buffer) != 0 {
			// An undelimited string item has already been started
			for {
				// Test for an item-terminating character
				switch ch {
				case ' ', '\n', 0:
					//break
				case ':', ',', ']', '}':
					// "unread" the character
					ch_index--
					//break
				case '{', '[', '\\', '"':
					panic(fmt.Sprintf(
						"Unexpected character ('%c') at position %s",
						ch,
						pos(here())))
				default:
					read_buffer = append(read_buffer, ch)
					ch = read_ch(false)
					continue
				}
				break
			}
			// Check whether macro name
			if read_buffer[0] == '&' {
				rstring := MinionString(read_buffer)
				mm, ok := macros[rstring]
				if ok {
					// Push to remember stack, marking it as not the owner of
					// its data
					remembered_items = append(remembered_items,
						MinionValue{mm.Type, mm.Flags + F_MACRO_VALUE, mm.Data})
					result = mm.Type
					break
				}
				// An undefined macro name
				new_String(string(read_buffer), T_NoType, F_Macro)
				result = F_Macro
				break
			}
			// A String without delimiters
			new_String(string(read_buffer), T_String, F_Simple_String)
			result = T_String
			break
		}

		// Look for start of next item
		switch ch {
		case 0:
			result = F_Token_End // end of input, no next item
			//break
		case ' ', '\n':
			continue // continue seeking start of item
		case '#': // start comment
			ch = read_ch(false)
			if ch == '[' {
				// Extended comment: read to "]#"
				comment_pos := here()
				ch = read_ch(false)
				for {
					if ch == ']' {
						ch = read_ch(false)
						if ch == '#' {
							break
						}
						continue
					}
					if ch == 0 {
						panic(fmt.Sprintf(
							"Unterminated comment ('\\[ ...') at position %s",
							pos(comment_pos)))
					}
					// Comment loop ... read next character
					ch = read_ch(false)
				}
				// End of extended comment
			} else {
				// "Normal" comment: read to end of line
				for {
					if ch == '\n' || ch == 0 {
						break
					}
					ch = read_ch(false)
				}
			}
			continue // continue seeking item
		case '"': // delimited string
			result = get_string()
			//break
		case '[': // list
			result = get_list()
			//break
		case '{': // map
			result = get_map()
			//break

		// further structural symbols
		case ']':
			result = F_Token_ListEnd
			//break
		case '}':
			result = F_Token_MapEnd
			//break
		case ':':
			result = F_Token_Colon
			//break
		case ',':
			result = F_Token_Comma
			//break
		default:
			read_buffer = append(read_buffer, ch) // start undelimited string
			continue
		} // End of switch
		break
	} // End of item-seeking loop
	return result
}

// Build a new String item from a char*. Place the result on the
// remember stack.
func new_String(text string, stype FlagType, sflags FlagType) {
	m := MinionValue{stype, sflags, MinionString(text)}
	remembered_items = append(remembered_items, m)
}

func MinionRead(input string) (doc MinionDoc, error_message string) {
	remembered_items = remembered_items[:0]
	macros = MinionMacros{}
	ch_input = input

	// Catch errors (panic calls)
	defer func() {
		if r := recover(); r != nil {
			e, ok := r.(string)
			if ok {
				error_message = e
			} else {
				panic(r)
			}
		}
	}()

	doc = read_doc()
	return doc, error_message
}

func read_doc() MinionDoc {
	ch_index = 0
	ch_linestart = 0
	line_index = 0
	var current_pos position
	for {
		current_pos = here()
		mtype := get_item()
		if mtype != F_Macro {
			// Check for macro redefinition
			if (remembered_items[len(remembered_items)-1].Flags & F_MACRO_VALUE) != 0 {
				panic(fmt.Sprintf(
					"Macro definition at position %s: name not unique",
					pos(current_pos)))
			} else if real_minion_value(mtype) {
				// found document item
				break
			}
			// Invalid item
			if mtype == F_Token_End {
				panic("Document contains no main item")
			}
			panic(fmt.Sprintf(
				"Invalid minion item at position %s",
				pos(current_pos)))
		}
		// *** macro name: read the definition ***
		current_pos = here()
		// expect ':'
		mtype = get_item()
		if mtype != F_Token_Colon {
			panic(fmt.Sprintf(
				"Expecting ':' at position %s in macro definition",
				pos(current_pos)))
		}
		current_pos = here()
		mtype = get_item()
		// expect value
		if real_minion_value(mtype) {
			// expect ','
			mtype = get_item()
			if mtype == F_Token_Comma {
				// Add the macro, taking on ownership of the
				// allocated memory
				mname := remembered_items[0].Data.(MinionString)
				mval := remembered_items[1]
				remembered_items = remembered_items[:0]
				macros[mname] = mval
				continue
			}
			panic(fmt.Sprintf(
				"After macro definition: expecting ',' at position %s",
				pos(current_pos)))
		}
		panic(fmt.Sprintf(
			"In macro definition, expecting a value at position %s",
			pos(current_pos)))
	}
	// "Real" minion item, not macro definition => document content
	doc := MinionDoc{Item: remembered_items[0], Macros: macros}

	// Check that there are no further items
	current_pos = here()
	if get_item() != F_Token_End {
		panic(fmt.Sprintf(
			"Position %s: unexpected item after document item",
			pos(current_pos)))
	}
	return doc
}

// The "get_" family of functions reads the corresponding item from the
// input. If the result is a minion item, that will be placed on the
// remember stack. The "get_" functions return the type of the item that
// was read.

/* Read a delimited string (terminated by '"') from the input.
 *
 * It is entered after the initial '"' has been read, so the next character
 * will be the first of the string.
 *
 * Escapes, introduced by '\', are possible. These are an extension of the
 * JSON escapes – see the MINION specification.
 */
func get_string() FlagType {
	start_pos := here()
	var ch byte = 0
	for {
		ch = read_ch(true)
		if ch == '"' {
			break
		}
		if ch == 0 {
			panic(fmt.Sprintf(
				"End of data reached inside delimited string from position %s",
				pos(start_pos)))
		}
		if ch == '\\' {
			ch = read_ch(false) // '\n' etc. are permitted here
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
				add_unicode_to_read_buffer(4)
				continue
			case 'U':
				add_unicode_to_read_buffer(6)
				continue
			case '[':
				// embedded comment, read to "\]"
				{
					comment_pos := here()
					ch = read_ch(false)
					for {
						if ch == '\\' {
							ch = read_ch(false)
							if ch == ']' {
								break
							}
							continue
						}
						if ch == 0 {
							panic(fmt.Sprintf(
								"End of data reached inside string comment from position %s",
								pos(comment_pos)))
						}
						// loop with next character
						ch = read_ch(false)
					}
				}
				continue // comment ended, seek next character
			default:
				panic(fmt.Sprintf(
					"Illegal string escape at position %s",
					pos(here())))
			}
		}
		read_buffer = append(read_buffer, ch)
	}
	new_String(string(read_buffer), T_String, F_NoFlags)
	return T_String
}

func add_unicode_to_read_buffer(l int) {
	buf := make([]byte, l)
	for i := range l {
		buf[i] = read_ch(false)
	}
	number, err := strconv.ParseUint(string(buf), 16, 0) // Parse as base 16
	if err == nil {
		s := string(rune(number))
		// Check validity (if invalid, the string will be "\uFFFD")
		if s != "\uFFFD" {
			// Add new character to read_buffer
			read_buffer = append(read_buffer, s...)
			return
		}
	}
	panic(fmt.Sprintf(
		"Invalid Unicode escape in string, position %s",
		pos(here())))
}

func real_minion_value(mtype FlagType) bool {
	return (mtype != T_NoType && mtype < MIN_FLAG)
}

func get_list() FlagType {
	start_index := len(remembered_items)
	current_pos := here()
	mtype := get_item()
	for {
		// ',' before the closing bracket is allowed
		if mtype == F_Token_ListEnd {
			break
		}
		if real_minion_value(mtype) {
			current_pos = here()
			mtype = get_item()
			if mtype == F_Token_ListEnd {
				break
			}
			if mtype == F_Token_Comma {
				current_pos = here()
				mtype = get_item()
				continue
			}
			panic(fmt.Sprintf(
				"Reading list, expecting ',' or ']' at position %s",
				pos(current_pos)))
		}
		if mtype == F_Macro {
			panic(fmt.Sprintf(
				"Undefined macro name at position %s",
				pos(current_pos)))
		} else {
			panic(fmt.Sprintf(
				"Expecting list item or ']' at position %s",
				pos(current_pos)))
		}
	}
	new_Array(start_index)
	return T_Array
}

// Build a new Array item from items on the stack, the starting index
// being passed as argument.
// Place the result on the remember stack.
func new_Array(start_index int) {
	m := slices.Clone(remembered_items[start_index:])
	remembered_items = remembered_items[:start_index+1]
	remembered_items[start_index] = MinionValue{T_Array, F_NoFlags, MinionArray(m)}
}

// *** Handle the map type as PairArray
func (m *MinionPairArray) Find(key MinionString) (MinionValue, bool) {
	for _, v := range *m {
		if v.Key == key {
			return v.Value, true
		}
	}
	return MinionValue{}, false
}

func get_map() FlagType {
	start_index := len(remembered_items)
	current_pos := here()
	m := MinionPairArray{}
	mtype := get_item()
	var seeking string
	for {
		// ',' before the closing bracket is allowed
		if mtype == F_Token_MapEnd {
			break
		}
		// expect key
		if mtype == T_String {
			key := remembered_items[len(remembered_items)-1].Data.(MinionString)
			if _, ok := m.Find(key); ok {
				panic(fmt.Sprintf(
					"Map key not unique: %s (at position %s)",
					key,
					pos(current_pos)))
			}
			current_pos = here()
			mtype = get_item()
			// expect ':'
			if mtype != F_Token_Colon {
				panic(fmt.Sprintf(
					"Expecting ':' in Map item at position %s",
					pos(current_pos)))
			}
			current_pos = here()
			mtype = get_item()
			// expect value
			seeking = "Reading map, expecting a value at position %s"
			if real_minion_value(mtype) {
				current_pos = here()
				mtype = get_item()
				if mtype == F_Token_MapEnd {
					break
				} else if mtype == F_Token_Comma {
					current_pos = here()
					mtype = get_item()
					continue
				}
				panic(fmt.Sprintf(
					"Reading map, expecting ',' or '}' at position %s",
					pos(current_pos)))
			} else if mtype == F_Macro {
				seeking = "Expecting map value, undefined macro name at position %s"
			}
		} else {
			seeking = "Reading map, expecting a key at position %s"
		}
		panic(fmt.Sprintf(seeking, pos(current_pos)))
	}
	new_PairArray(start_index)
	return T_PairArray
}

// Build a new PairArray item from items on the stack, the starting index
// being passed as argument.
// Place the result on the remember stack.
func new_PairArray(start_index int) {
	l := len(remembered_items) - start_index
	if (l & 1) != 0 {
		// Each entry is a pair, i.e. it consists of two items.
		panic("[BUG] In new_PairArray: odd number of items on stack")
	}
	l /= 2
	m := make(MinionPairArray, 0, l)
	i := start_index
	for range l {
		m = append(m, MinionPair{
			remembered_items[i].Data.(MinionString),
			remembered_items[i+1]})
		i += 2
	}
	remembered_items = remembered_items[:start_index+1]
	remembered_items[start_index] = MinionValue{T_PairArray, F_NoFlags, m}
}

// *** dump functions (serializing)

func dump_string(source string) {
	dump_buffer = append(dump_buffer, '"')
	var ch byte
	for _, ch = range []byte(source) {
		if ch >= 32 {
			if ch == 127 {
				dump_buffer = append(dump_buffer, '\\', 'u', '0', '0', '7', 'F')
				continue
			}
			dump_buffer = append(dump_buffer, ch)
			continue
		}
		switch ch {
		case '"':
			dump_buffer = append(dump_buffer, '\\', '"')
			//break
		case '\n':
			dump_buffer = append(dump_buffer, '\\', 'n')
			//break
		case '\t':
			dump_buffer = append(dump_buffer, '\\', 't')
			//break
		case '\b':
			dump_buffer = append(dump_buffer, '\\', 'b')
			//break
		case '\f':
			dump_buffer = append(dump_buffer, '\\', 'f')
			//break
		case '\r':
			dump_buffer = append(dump_buffer, '\\', 'r')
			//break
		case '\\':
			dump_buffer = append(dump_buffer, '\\', '\\')
			//break
		default:
			dump_buffer = append(dump_buffer, '\\', 'u', '0', '0')
			if ch >= 16 {
				dump_buffer = append(dump_buffer, '1')
				ch -= 16
			} else {
				dump_buffer = append(dump_buffer, '0')
			}
			if ch >= 10 {
				dump_buffer = append(dump_buffer, 'A'+ch-10)
			} else {
				dump_buffer = append(dump_buffer, '0'+ch)
			}
		}
	}
	dump_buffer = append(dump_buffer, '"')
}

func dump_pad(n int) {
	if n >= 0 {
		dump_buffer = append(dump_buffer, '\n')
		n *= indentation
		for n > 0 {
			dump_buffer = append(dump_buffer, ' ')
			n--
		}
	}
}

func dump_list(source MinionValue, indent int) bool {
	var new_depth int = -1
	if indent >= 0 {
		new_depth = indent + 1
	}
	dump_buffer = append(dump_buffer, '[')
	for _, m := range source.Data.(MinionArray) {
		dump_pad(new_depth)
		if !dump_value(m, new_depth) {
			return false
		}
		dump_buffer = append(dump_buffer, ',')
	}
	dump_buffer = dump_buffer[:len(dump_buffer)-1] // remove last ','
	dump_pad(indent)
	dump_buffer = append(dump_buffer, ']')
	return true
}

func dump_map(source MinionValue, indent int) bool {
	var new_depth int = -1
	if indent >= 0 {
		new_depth = indent + 1
	}
	dump_buffer = append(dump_buffer, '{')
	for _, m := range source.Data.(MinionPairArray) {
		dump_pad(new_depth)
		dump_string(string(m.Key))
		dump_buffer = append(dump_buffer, ':')
		if indent >= 0 {
			dump_buffer = append(dump_buffer, ' ')
		}
		if !dump_value(m.Value, new_depth) {
			return false
		}
		dump_buffer = append(dump_buffer, ',')
	}
	dump_buffer = dump_buffer[:len(dump_buffer)-1] // remove last ','
	dump_pad(indent)
	dump_buffer = append(dump_buffer, '}')
	return true
}

func dump_value(source MinionValue, indent int) bool {
	var ok bool = true
	switch source.Type {
	case T_String:
		// Strings don't receive any extra formatting
		s := source.Data.(MinionString)
		dump_string(string(s))
		//break
	case T_Array:
		ok = dump_list(source, indent)
		//break
	case T_PairArray:
		ok = dump_map(source, indent)
		//break
	default:
		ok = false
	}
	return ok
}

func MinionDump(source MinionValue, indent int) (string, bool) {
	dump_buffer = dump_buffer[:0] // reset dump_buffer
	if dump_value(source, indent) {
		return string(dump_buffer), true
	}
	return "", false
}
