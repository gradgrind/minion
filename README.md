# MINION – MINImal Object Notation

MINION is a mark-up language for simple data transfer, configuration files, etc. – similar to JSON, but with more emphasis on writing manually.

JSON is actually valid MINION, but not necessarily the other way round. MINION is simpler in the sense that it has no data types apart from strings, but extends JSON in a few ways to make it more convenient for writing by hand. See [minion.md](minion.md) for more details.

Initial implementations (serializers and deserializers) are provided in C, C++ and Go. The current serializers output valid JSON (which can also be read back in by the deserializers). There is also a primitive C++ version (minion_cxx_c), which is just the C version modified to run through a C++ compiler ...

The current versions, except minion_cxx, have static variables holding state information. In minion_cxx the state information is held in class instances.

The C(++) versions have a main file and the Go version has a test file which show the basic usage of the functions.

The main.c of the C version  (the library itself comprises only minion.c and minion.h) uses a C++ function to read a file ... I suppose I should rewrite this in C! When using minion_c (or minion_cxx_c), some attention to memory management is necessary, but I have tried to keep this fairly simple and efficient. Memory management in minion_cxx and minion_go should be easier.

I have tried to provide helpful error messages, but no i18n is supported at present.

Maps are represented as arrays of key/value pairs. This has the (possible) advantage of retaining input order trivially, but can be inefficient when the number of keys grows.

The code has been subject to only limited testing.
