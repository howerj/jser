/* Author:  Richard James Howe
 * Project: JSON Serialization Routines
 * License: MIT (for 'jsmn.h'), Proprietary for everything else.
 *
 * This set of routines allow you to serialize a specially formatted
 * C structure that points to variables within your C program to and
 * from JSON.
 *
 * See the project 'jser.md' file for information */
#ifndef JSER_H
#define JSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

#define JSMN_HEADER
#define JSMN_PARENT_LINKS
#include "jsmn.h"

#ifndef JSER_LONG_T
typedef long jser_long_t;
#endif

#ifndef JSER_ULONG_T
typedef unsigned long jser_ulong_t;
#endif

typedef enum {
    JSER_LONG_E,    /**< a signed long (at least 32-bits) */
    JSER_ULONG_E,   /**< an unsigned long (at least 32-bits) */
    JSER_BOOL_E,    /**< a boolean value */
    JSER_ASCIIZ_E,  /**< a NUL terminated string */
    JSER_BUFFER_E,  /**< a binary buffer, serialized to a base64 encoded string */
    JSER_OBJECT_E,  /**< a JSON object */
    JSER_ARRAY_E,   /**< a JSON array */
} jser_type_e; /**< type of value we want to serialize/deserialize*/

typedef struct {
    size_t length,      /**< length of buffer in bytes */
           used;        /**< amount actually used in buffer, in bytes */
    unsigned char *buf; /**< buffer, needs to be at least 'length' bytes long */
} jser_buffer_t; /**< used to hold binary data */

struct jser;
typedef struct jser jser_t;

typedef union {
    jser_long_t *ld;
    jser_ulong_t *lu;
    bool *b;
    char *asciiz; /**< may only be serialized into JSON if length not specified */
    jser_buffer_t *buf;
    jser_t *jser;
    jser_t *array;
} jser_type_u; /**< union of pointers to all data types we can handle */

struct jser { /**< The main jser object used for serialization */
    const char *attr;      /**< attribute of this element, must be set unless member is part of an array */
    size_t length, used;   /**< length of data we are pointing to, and amount we have actually used */
    jser_type_e type;      /**< type of data we are pointing to */
    jser_type_u data;      /**< pointer to data */
    bool is_array;         /**< do we actually have an array of 'jser_type_u'? */
};

/* all function return 0 on success, negative on failure */
int jser_base64_decode(const unsigned char *ibuf, const size_t ilen, unsigned char *obuf, size_t *olen);
int jser_base64_encode(const unsigned char *ibuf, size_t ilen, unsigned char *obuf, size_t *olen);
int jser_serialize_to_buffer(const jser_t *j, size_t jlen, int pretty, jser_buffer_t *b);
int jser_serialize_to_asciiz(const jser_t *j, size_t jlen, int pretty, char *asciiz, size_t length); /* NUL terminates 'asciiz' on success */
int jser_serialized_length(const jser_t *j, size_t jlen, int pretty, size_t *sz); /* excludes NUL terminator */
int jser_deserialize_from_buffer(jser_t *j, size_t jlen, jsmntok_t *t, size_t tokens, jser_buffer_t *b);
int jser_deserialize_from_asciiz(jser_t *j, size_t jlen, jsmntok_t *t, size_t tokens, const char *asciiz);
int jser_retrieve_node(const jser_t *j, size_t jlen, jser_t **const found, const char *path);  /* 1 = found, 0 = not found, <0 = failure */
int jser_walk_tree(const jser_t *j, size_t jlen, int (*fn)(const jser_t *e, void *param), void *param);
int jser_copy(const jser_t *src, const size_t slen, jser_t *pool, size_t *plen);
int jser_node_count(const jser_t *j, size_t *jlen);
int jser_version(unsigned long *version); /* version in x.y.z format, LSB = z, MSB = options */
int jser_tests(void);

#define MK_LONG(X)   { .attr = (#X), .type = JSER_LONG_E,   .data.ld     = &(X), }
#define MK_ULONG(X)  { .attr = (#X), .type = JSER_ULONG_E,  .data.lu     = &(X), }
#define MK_BOOL(X)   { .attr = (#X), .type = JSER_BOOL_E,   .data.b      = &(X), }
#define MK_ASCIIZ(X) { .attr = (#X), .type = JSER_ASCIIZ_E, .data.asciiz =  (X), }
#define MK_BUF(X)    { .attr = (#X), .type = JSER_BUFFER_E, .data.buf    = &(X), }
#define MK_ARRAY(X)  { .attr = (#X), .type = JSER_ARRAY_E,  .data.array  =  (X), .length = ELEMENTS((X)), .used = ELEMENTS((X)), }
#define MK_OBJECT(X) { .attr = (#X), .type = JSER_OBJECT_E, .data.jser   =  (X), .length = ELEMENTS((X)), .used = ELEMENTS((X)), }

#define MK_NAMED_LONG(X, NAME)   { .attr = (NAME), .type = JSER_LONG_E,   .data.ld     = &(X), }
#define MK_NAMED_ULONG(X, NAME)  { .attr = (NAME), .type = JSER_ULONG_E,  .data.lu     = &(X), }
#define MK_NAMED_BOOL(X, NAME)   { .attr = (NAME), .type = JSER_BOOL_E,   .data.b      = &(X), }
#define MK_NAMED_ASCIIZ(X, NAME) { .attr = (NAME), .type = JSER_ASCIIZ_E, .data.asciiz =  (X), }
#define MK_NAMED_BUF(X, NAME)    { .attr = (NAME), .type = JSER_BUFFER_E, .data.buf    = &(X), }
#define MK_NAMED_ARRAY(X, NAME)  { .attr = (NAME), .type = JSER_ARRAY_E,  .data.array  =  (X), .length = ELEMENTS((X)), .used = ELEMENTS((X)), }
#define MK_NAMED_OBJECT(X, NAME) { .attr = (NAME), .type = JSER_OBJECT_E, .data.jser   =  (X), .length = ELEMENTS((X)), .used = ELEMENTS((X)), }

#ifdef __cplusplus
}
#endif
#endif
