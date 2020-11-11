# JSER: JSON Serialization Module

* Author: Richard James Howe
* License: MIT (for [jsmn.h][]), Proprietary for everything else.
* Copyright: Chameleon Technology.
* Project: JSER
* Brief: JSON serialization/deserialization in an embedded device

This module/library can be used to deserialize/serialize a list of objects
from/into [JSON][]. The idea is that the user passes in a specification which
contains the attribute name, a type and a pointer of the data type to be serialized
and the JSER library turns this into a JSON string containing that data.

The library can turn a limited number of types into JSON; booleans, signed
longs, unsigned longs, NUL terminated strings ([ASCIIZ][] strings), a buffer
object, and other lists of objects which may be treated as arrays or nested
JSON objects.

For a full list of types and example code, refer to the unit tests at the
end of [jser.c][] and the examples in [main.c][].

## Serialization/Deserialization

Imagine if we had a module that contained a number of variables or possible
a structure that we wanted to convert to and from JSON. There are a few ways
of going about this.

1. We could make a custom function for serialization for each object.
2. We could feed in a JSON schema to a code generator that could generate
custom functions for serialization of each object. The structure we want
to process could potentially be the schema, but a language agnostic schema
would be more appropriate.
3. If the language supports [reflection][] then we could write a single function
for serialization and another for deserialization that could inspect the data
object to be processed at run time.
4. If the language does not support reflection would could manually specify
the data that would be needed in lieu of reflection, and hand that off to
functions for serialization and deserialization.

Each option has various trade offs. As [C][] lacks capability for reflection
option '3' can be ruled out. Option '1' is error prone, potentially the fastest,
more brittle, and the most flexible. Option '2' is contingent on finding a good
code generator, suitable for embedded use on a microcontroller. There are
code generators, but they are for use in a hosted environment. That leaves us
with option '4', which the **jser** library implements.

The library must not perform any internal allocation for either the deserialization
or the serialization, it should also have minimal dependencies on the [C][] standard
library (only using the most basic string functions). [malloc][] and [free][] are
of course banned. This places more restrictions on the deserialization routines than
it does the serialization routines as it must parse some arbitrary JSON.

To describe JSON that we want to change to and from variables we will need
a structure capable of doing this. This is provided for by the 'jser\_t' structure,
which can be viewed in [jser.h][]. The user must provide a pointer to an array
of these objects and the length of the array to these functions:

	int jser_serialize_to_buffer(const jser_t *j, size_t jlen, int pretty, jser_buffer_t *b);
	int jser_serialize_to_asciiz(const jser_t *j, size_t jlen, int pretty, char *asciiz, size_t length);
	int jser_serialized_length(const jser_t *j, size_t jlen, int pretty, size_t *sz);
	int jser_deserialize_from_buffer(jser_t *j, size_t jlen, jser_token_t *t, size_t tokens, jser_buffer_t *b);
	int jser_deserialize_from_asciiz(jser_t *j, size_t jlen, jser_token_t *t, size_t tokens, const char *asciiz);

Each element to be serialized must be describe by the following structure:


	struct jser { /**< The main jser object used for serialization */
		const char *attr;      /**< attribute of this element, must be set unless member is part of an array */
		size_t length, used;   /**< length of data we are pointing to, and amount we have actually used */
		jser_type_e type;      /**< type of data we are pointing to */
		jser_type_u data;      /**< pointer to data */
		unsigned is_array:  1; /**< do we actually have an array of 'jser_type_u'? */
	};

The library contains many examples for both serialization and deserialization
within it in the form of tests, and because those examples have to both compile
and run they are more likely to be up to date and correct than the documentation
here. The built in tests should be considered the primary documentation for how
this library works.

Having said that. On to the examples.

Many of the fields present are optional, or conditionally optional. Each element
must have a pointer to a valid value of the correct type. Imagine we want to
describe something that can serialize and describe the following JSON:

	{
		"a" : 1,
		"b" : 2,
		"c" : [ 3, 4, 5 ],
		"d" : {
			"e" : 6,
			"f" : 7,
		},
		"g" : 8
	}

We would need the following elements and values to describe the data to
the *jser* functions:

	#define ELEMENTS(X) (sizeof(X) / sizeof(X[0]))

	jser_long_t a = 0,  b = 0,  c[3] = { 0 },  d = 0,  e = 0,  f = 0,  g = 0;

	jser_t c[] = {
		{ .type = jser_long_e, .data.ld = &c[0] },
		{ .type = jser_long_e, .data.ld = &c[1] },
		{ .type = jser_long_e, .data.ld = &c[2] },
	};

	jser_t d[] = {
		{ .attr = "e", .type = jser_long_e, .data.ld = &e },
		{ .attr = "f", .type = jser_long_e, .data.ld = &f },
	};

	jser_t json[] = {
		{ .attr = "a", .type = jser_long_e,   .data.ld    = &a },
		{ .attr = "b", .type = jser_long_e,   .data.ld    = &b },
		{ .attr = "c", .type = jser_array_e,  .data.array = c, .length = ELEMENTS(c), .used = ELEMENTS(c) },
		{ .attr = "d", .type = jser_object_e, .data.jser  = d, .length = ELEMENTS(d), .used = ELEMENTS(d) },
		{ .attr = "g", .type = jser_long_e,   .data.ld    = &g },
	};

This simple example shows how we can process arbitrarily nested arrays and
objects. We could then use this structure to serialize this data into a
string, or from a string.

	char obuf[512] = { 0 };

	if (jser_serialize_to_asciiz(json, ELEMENTS(json), 0, obuf, ELEMENTS(obuf) < 0) {
		return -1; /* error */
	}

	printf("%s\n", obuf); /* prints out JSON */

To deserialize we also need to provide an array of tokens from a pool than can
be used to parse the JSON input string. The size of this should be made to be
as long as possible, if it is too small then deserialization will fail. It would
be best to create a common pool of 'jser\_token\_t'. The only interaction that
the user needs to have with this pool is the allocation of it and passing it
to the deserialization function - do not worry about the internals of that structure,
for reference the [jsmn][] library is actually used to parse the JSON.

	/* ibuf holds our JSON to process */
	char ibuf[] = "{\"a\":1,\"b\":2,\"c\":[3,4,5],\"d\":{\"e\":6,\"f\":7,},\"g\":8}";

	jser_token_t tokens[128];

	if (jser_deserialize_from_asciiz(json, ELEMENTS(json), ELEMENTS(json), tokens, ELEMENTS(tokens), ibuf) < 0) {
		return -1; /* error */
	}

	printf("a = %ld", a); /* prints 'a = 1' */
	printf("b = %ld", b); /* prints 'b = 2' */
	/* etcetera */
	printf("g = %ld", g); /* prints 'g = 8' */

### Common Errors

Whilst the library aims at making C to JSON conversion easier by making it driven
by data instead of code, there are still some problems that cannot be fixed.

* Not specifying the *length* or *used* field.
* Using the incorrect *type* for the data. Unfortunately there is no way in [C][] to
enforce that the contents of a union are checked against the type field that union when
using a tagged union. As such, these simply type errors can occur.
* Trying to deserialize to an *ASCIIZ* string without specifying a length. The *length* field
must be specified when trying to write to an ASCIIZ string.
* Trying to deserialize to a string stored in constant storage.

### Known Bugs

There are currently no known bugs, for bugs, please contact
<mailto:richard.howe@chameleontechnology.co.uk>.

## Miscellaneous Functions

There are a few miscellaneous functions within the library. These are just
some nice helper functions.

### jser\_retrieve\_node

The 'jser\_retrieve\_node' function can be used to lookup a node within the of
items by name. The function is inspired by [XPath][], but it is currently incredibly
limited. It can only search for nodes, and can only search within objects and not
within arrays. The names also cannot have the '/' character in them as this is
used as a path separator:

	 /* 1 = found, 0 = not found, <0 = failure */
	int jser_retrieve_node(const jser_t *j, size_t length, jser_t **found, const char *path);

The function returns 1 if the node has been found, 0 if it has not been found,
and any negative value indicates a problem.

Given the JSON:

	{ "j": { "a" : 123, "b" : 456}, "c" : 789 }

And the configuration:

	long a = 0, b = 0, c = 0;

	jser_t j[] = {
		{ .attr = "a", .type = jser_long_e, .data.ld = &a },
		{ .attr = "b", .type = jser_long_e, .data.ld = &b },
	};

	jser_t config[] = {
		{ .attr = "j", .type = jser_object_e, .data.jser = j, .length = ELEMENTS(j), .used = ELEMENTS(j) },
		{ .attr = "c", .type = jser_long_e,   .data.ld = &c   },
	};

The node 'b' with 'j' can be queried with the path string "j/b". The
element for 'b' is returned in the 'found' variable.

### jser\_walk\_tree

The function 'jser\_walk\_tree' can be used to walk the tree of 'jser\_t' elements
and apply a function to each element within the tree. The tree walking is done depth
first, pre-order.

	int jser_walk_tree(const jser_t *j, size_t length, int (*fn)(jser_t *e, void *param), void *param);

This function is quite limited, and only visits each node once. A more useful tree
walking function would allow the order that each node is visited to be changed, and
also allow a mode to call the callback and tell it when we are entering and exiting
a branch node.

### jser\_copy and jser\_node\_count

The function 'jser\_copy' is used to copy a 'jser\_t' tree, allocating nodes in the
tree from a pool. Allocation starts at the first node in the pool and the number of
nodes allocate will be returned in 'plen'. 'plen' should initially contain the number
of nodes in the pool.

'jser\_node\_count' can be used to determine how many nodes will need to be allocated
in the pool.

### jser\_version

The function 'jser\_version' retrieves the version number for the library and what options
where set when this library was compiled. It can return an error if the version
number has not been set by the build system.

	int jser_version(unsigned long *version);

The format for the version number is:

	Least Significant Byte: z (Patch Version)
	Lower Middle Byte:      y (Minor Version)
	Upper Middle Byte:      x (Major Version)
	Most Significant Byte:  options

[Semantic Versioning][] is used.

The options byte has the following format:

	Bit 0:   Are tests enabled (1 = true, 0 = false)
	Bit 1:   Is escaping enable in generated JSON (1 = true, 0 = false)
	Bit 2-7: Unused

### jser\_tests

This function 'jser\_tests' runs the internal library tests. The function
returns negative if the internal tests fail for some reason, and zero on success.

	int jser_tests(void);

To save space the internal tests may be compiled out, the function will still
exists but it will always return zero (success). To find out whether this is
the case you can query the version function.


## License

The [jsmn.h][] header only C libary is licensed under the MIT license, see the
file for more information. This library is available at
<https://github.com/zserge/jsmn>. It is used for parsing/tokenizing JSON.

## To Do / Wish list

* [x] Implement JSON Serialization
* [x] Add documentation on how to use this library
* [x] Allow some control over formatting when pretty printing (spaces vs tabs, etcetera).
* [x] Add a function for calculating the size needed to store a serialized string.
* [x] Implement JSON Deserialization
* [x] A slightly different way of doing things may be to include a 'used'
  and 'length' field in 'jser\_element\_t' and turn those into arrays
  when encountered, this would allow us to remove the array type and also
  easily serialize/deserialize arrays of C objects. This also means we
  could remove the 'jser\_t' structure.
* [x] A foreach function that visits each node may be useful (especially if you
  can control the visitation order), this could have been used to construct a
  function for serialization.
* [ ] Add a function for copying a configuration from a pool of nodes, this
  will be required if multiple instances of the same object need to be serialized/
  deserialized.
* [x] Add more unit tests, and assertions.
  - [ ] Test failure cases in unit test such as recursion depth, running out of
  space in the output buffer...
  - [ ] Test input can cope with Windows style newlines
* [ ] Allow custom types to be serialized with callbacks. This would mostly
  be useful for enumerated types. All that is required is a 'convert to string'
  function for that enumeration to be registers.
* [ ] Instead of storing pointers to small value types (such as long integers
  and booleans), we could store the data in 'jser\_t' element itself, or have,
  and option to do so, this only make sense if we have functions for retrieving
  fields by attribute, or the path to that attribute (via a functional equivalent
  to [XPath][] for example). That retrieval function would be useful regardless of
  where we store out data.
* [ ] It is possible to use this library to produce a (simple) schema trivially.
  This could be done by modifying the serialization functions to print out types.
  See <https://json-schema.org/learn/miscellaneous-examples.html> and
  <https://json-schema.org/specification.html> for more information.
* [ ] The current system serializes to and from strings, however it is possible
  to serialize to a callback, which would allow processing data directly
  from a communications channel with as little copying as possible. It is partially
  possible to do this with deserialization as the [jsmn][] library can tell use when it
  has failed to parse the incoming data because only a partial packet has been received,
  but this requires much more thought/rework.
* [ ] This library would be well worth fuzzing.

[jsmn.h]: jsmn.h
[jser.c]: jser.c
[jser.h]: jser.h
[main.c]: main.c
[XPath]: https://en.wikipedia.org/wiki/XPath
[Semantic Versioning]: https://semver.org/
[C]: https://en.wikipedia.org/wiki/C_(programming_language)
[malloc]: http://www.cplusplus.com/reference/cstdlib/malloc/
[free]: http://www.cplusplus.com/reference/cstdlib/free/
[reflection]: https://en.wikipedia.org/wiki/Reflection_(computer_programming)
[JSON]: https://en.wikipedia.org/wiki/JSON
[ASCIIZ]: https://en.wikipedia.org/?title=ASCIIZ&redirect=no
