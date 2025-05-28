# MINION – MINImal Object Notation

MINION is a mark-up language for simple data transfer, configuration files, etc. – similar to JSON, but with more emphasis on writing manually.

JSON is actually valid MINION, but not necessarily the other way round. MINION is simpler in the sense that it has no data types apart from strings, but extends JSON in a few ways to make it more convenient for writing by hand. See [minion.md](minion.md) for more details.

Initial implementations (serializers and deserializers) are provided in C, C++ (currently several versions ...) and Go. The current serializers output valid JSON (which can also be read back in by the deserializers).

Simple test files are included in all versions. In the C(++) versions this is main.c(pp), in the Go version this is minion_test.go.

The library itself comprises only minion.c(pp) and minion.h in the C(++) versions, minion.go in the Go version.

 - minion_c: State information is held in static variables. Some attention to memory management is necessary, but I have tried to keep this fairly simple and efficient. The main.c test file uses a C++ function to read a file ... I suppose I should rewrite this in C!  

 - minion_cxx_c: a primitive C++ version which is just the C version modified to run through a C++ compiler ...

 - minion_cxx: The state information is held in class instances. This is a redesign using more C++ features. It shares data without using the standard C++ mechanism (shared_ptr). The memory is managed as a complete structure, rather than managing the MValue elements individually.

 - minion_cxx_shared: another rewrite, this time using shared_ptr.

I have tried to provide helpful error messages, but no i18n is supported at present.

Maps are represented as arrays of key/value pairs. This has the (possible) advantage of retaining input order trivially, but can be inefficient when the number of keys grows.

The code has been subject to only limited testing.
