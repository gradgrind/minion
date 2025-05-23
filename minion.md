# MINION: MINImal Object Notation, version 1.0 (2025-05-23)

MINION is a simple data-transfer format closely related to JSON. The main aim was to have a format which makes it a bit easier to write configuration files and other structured data manually than it would be with JSON, without going in a completely different design direction. To keep it simple, there is only a single data type, the string. Thus any problems with the numeric values in JSON (only float, limited range, etc.) are avoided by not having any numbers – if they are needed, conversions must be done at the receiving end, where they can be adapted to the needs of the application. In other areas MINION is more flexible than JSON:

 - Single-word strings without certain characters can be used without `"` delimiters. This is particularly useful for map (~ JSON object) keys.

 - String escapes are the same as in JSON, but there is an additional `\Uxxxxxx` form to represent 6-digit hexadecimal Unicode points.

 - Comments are supported, including multiline ones. A form of multiline comment is also supported within delimited strings. This allows spreading a string over several lines.

 - The MINION parser can read JSON. It won't recognize numbers and booleans, but otherwise there should be no problems.

 - It is easy to output JSON from the structure resulting from parsing MINION.

The structuring elements are the same as in JSON – lists (arrays) and maps (objects). The only supported character encoding is UTF-8. Most of the ASCII control characters (0-31 and 127) are not allowed and should be reported as errors. The permitted exceptions are `\n`, `\t`, `\r` as layout/spacing characters, but not within strings. There are other Unicode characters which should probably be avoided, but no checks are made as this is generally a difficult problem and it is perhaps not clear where the line should be drawn.

## Details

A string may be enclosed in quotation marks (`" ... "`), but this is not necessary if no "special" characters are included in the string.

Whitespace characters (space, newline, carriage return, tab) are not necessary as separators, but can be added freely to "prettify" the document.

A plain comment starts with a `#` character and continues to the end of the line. However, if the `#` is directly followed by `[`, the comment is only terminated by `]#` and can continue over line breaks.

The "special" characters are:
 - "whitespace" characters (space, newline, etc.) – not allowed in strings, apart from the space character, which is allowed in delimited strings
 - `#`: starts a comment
 - `:`: separates key from value in a map
 - `{`: starts a map
 - `}`: ends a map
 - `[`: starts a list
 - `]`: ends a list
 - `"`: string delimiter
 - `\`: string "escape" character (special in delimited strings only)
 - `,`: separates elements in a list or map – unlike in JSON also the last element may be followed by a comma

### Maps

A map (like a JSON object) associates a value with a key:

```
    { key1:value1, key2:value2 }
```

 - A "key" is a string.
 - A "value" may be a string, a list or a map.

Note that the prototype parser uses a list of key-value pairs to represent a map. This means that the order of the entries is retained, but look-ups will be inefficient – or even very inefficient – for large maps. 
 
### Lists

A list (like a JSON array) is an ordered collection of values:

```
[ value1, value2 ]
```

 - A "value" may be a string, a list or a map.

### Strings

Certain characters are not directly possible in a string; they may, however, be included in delimited strings by means of escape sequences:

 - `"`: `\"`
 - `\`: `\\`
 - `/`: `\/` (optional, like in JSON)
 - newline: `\n`
 - tab: `\t`
 - backspace: `\b`
 - carriage return: `\r`
 - form-feed: `\f`
 - hexadecimal Unicode character: `\uxxxx` or `\Uxxxxxx`

In addition, it is possible to have an "embedded comment" in a delimited string. This starts with `\[` and is ended by `\]`. As it may include newlines, this may be used to split a string over several lines.

### The "top level"

A MINION document consists of a single item. This could be a string, but only a list or map is likely to be useful.
 
### "Macro" feature

There is a very limited macro-like feature. Replacement items – key/value pairs, like in a map – may be defined before the document's top-level item. The key must be a non-delimited string beginning with `&`. The value can be any item. Whenever this key is used later in the input, it will be replaced by its associated value. Like in maps, macro definitions must be separated by commas. Also the last macro definition is separated from the document item by a comma.


**A macro example**

```
&MACRO1: [A "list of" words] # a 3-element list,

&MACRO2: {This: &MACRO1, That: "Something else"},

[ # The document object
  {
    "First item": [1, &MACRO1],
    "Second item": [2, &MACRO2]
  },

  "More stuff ..."
]
```

The resulting document item would then be something like (this form is also valid JSON):

```
[
  {
    "First item": [
      "1",
      [
        "A",
        "list of",
        "words"
      ]
    ],
    "Second item": [
      "2",
      {
        "This": [
          "A",
          "list of",
          "words"
        ],
        "That": "Something else"
      }
    ]
  },
  "More stuff ..."
]
```
