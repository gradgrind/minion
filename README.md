# MINION – MINImal Object Notation

MINION is a mark-up language for simple data transfer, configuration files, etc. – similar to JSON, but with more emphasis on writing manually.

JSON is actually valid MINION, but not necessarily the other way round. MINION is simpler in the sense that it has no data types apart from strings, but extends JSON in a few ways to make it more convenient for writing by hand. See [minion.md](minion.md) for more details.

Initial implementations (serializers and deserializers) are provided in C and Go. The current serializers output valid JSON (which can also be read back in by the deserializers). There is also a C++ version, which isn't really C++, it's just the C version modified to run through a C++ compiler ...

The Go version has a test file which shows the basic usage of the functions.

The C library (which actually consists of only minion.c and minion.h) is provided with a main.c (and a C++ function to read a file ... I suppose I should rewrite this in C!) to show its basic usage. When using the C library, some attention to memory management is necessary, but I have tried to keep this fairly simple and efficient. I have tried to provide helpful error messages, but no i18n is supported at present.

Both versions use arrays of key/value pairs for the map/object representation. This has the (possible) advantage of retaining input order, but can be inefficient when the number of keys grows.

The code has been subject to only limited testing.