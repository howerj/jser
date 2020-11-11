/* Author:  Richard James Howe
 * Project: JSON Serialization Routines
 *
 * Test driver for 'jser.c' project */

#include "jser.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define ELEMENTS(X)  (sizeof(X) / sizeof(X[0]))

static int example1(void)
{
    jser_long_t l1 = 123, l2 = -456, l3 = -1;
    jser_ulong_t ul1 = 123, ul2 = 456, ul3 = 0, ul4 = 999;

    char str1[] = "HI";
    char str2[] = "BYE";
    char str3[] = "ABC";
    char str4[] = "A\tB\n\rC\\  \" escaped";

    bool b1 = true, b2 = false, b3 = false;
    unsigned char buf[512] = { 0 };
    jser_buffer_t b = { .length = sizeof buf, .used = 0, .buf = buf, };

    jser_t jnests[] = { MK_ULONG(ul3), MK_ULONG(ul4), MK_LONG(l3), MK_ASCIIZ(str3), };
    jser_t jarrs[]  = { MK_ULONG(ul1), MK_ULONG(ul2), MK_LONG(l2), MK_ASCIIZ(str3), };

    unsigned char bstr1[] = "HELLO";
    jser_buffer_t buf1 = { .used = sizeof (bstr1) - 1, .length = sizeof (bstr1), .buf = bstr1, };

    jser_t js[] = {
        MK_ULONG(ul1),
        MK_ULONG(ul2),
        MK_LONG(l1),
        MK_LONG(l2),
        MK_OBJECT(jnests),
        MK_ASCIIZ(str1),
        MK_ASCIIZ(str2),
        {  .attr  =  "a1",    .type  =  JSER_ARRAY_E,   .data.array   =  jarrs,   .length  =  ELEMENTS(jarrs),   .used  =  ELEMENTS(jarrs),  },
        MK_BOOL(b1),
        MK_BOOL(b2),
        MK_BOOL(b3),
        MK_ASCIIZ(str4),
        MK_BUF(buf1),
    };

    if (jser_serialize_to_buffer(js, ELEMENTS(js), 1, &b) < 0) {
        (void)fprintf(stderr, "serialization failed\n");
        return -1;
    }

    jser_t *found = NULL;
    const int st = jser_retrieve_node(js, ELEMENTS(js), &found, "jnests/ul3");
    if (st != 1) {
        (void)fprintf(stderr, "node not found");
        return -1;
    }

    return fprintf(stdout, "%s\n", buf) < 0 ? -1 : 0;
}

static int example2(void)
{
    jser_long_t long1 = 123, long2 = -456;
    char string1[] = "ABCDEF";

    jser_t elements[] = { MK_LONG(long1), MK_ASCIIZ(string1), MK_LONG(long2), };

    char output[512] = { 0 };
    if (jser_serialize_to_asciiz(elements, ELEMENTS(elements), 1, output, sizeof output) < 0) {
        fprintf(stdout, "Error in example code!\n");
        return -1;
    }
    return fprintf(stdout, "%s\n", output);
}

static int example3(void)
{
    bool b1 = false, b2 = false, b3 = false;
    unsigned char bstr1[128] = "";
    jser_buffer_t buf1 = { .used = sizeof bstr1, .length = sizeof bstr1, .buf = bstr1, };
    jser_long_t l1 = 0, l2 = 101, l3 = 99, l4 = 98, l5 = 96;
    jser_ulong_t ul1 = 823, ul2 = 23;

    const char *deser = "{\
	\"b1\":true,\
	\"b2\":true,\
	\"l1\":-987,\
	\"a1\":[1,2,4],\
	\"b1\":false,\
	\"j1\":{\"ul1\":444, \"ul2\":111, \"l2\":333},\
	\"buf1\": \"SEVMTE8A\"\
}";

    jsmntok_t tokens[128] = { { 0 }, };

    jser_t array[]  = { MK_LONG(l3),   MK_LONG(l4),   MK_LONG(l5), };
    jser_t nested[] = { MK_ULONG(ul1), MK_ULONG(ul2), MK_LONG(l2), };

    jser_t object[] = {
        MK_BOOL(b1), MK_BOOL(b2),
        MK_LONG(l1),
        {  .attr  =  "a1",   .type  =  JSER_ARRAY_E,   .data.array = array, .length = ELEMENTS(array), .used = ELEMENTS(array),  },
        MK_BOOL(b3),
        {  .attr  =  "j1",   .type  =  JSER_OBJECT_E,  .data.jser  = nested, .length = ELEMENTS(nested), .used = ELEMENTS(nested),  },
        MK_BUF(buf1),
    };

    const int dfail = jser_deserialize_from_asciiz(object, ELEMENTS(object), tokens, ELEMENTS(tokens), deser);
    if (dfail < 0) {
        fprintf(stderr, "deserialization failed! %d\n", dfail);
        return -1;
    }

    fprintf(stdout, "b1 = %d, b2 = %d, b3 = %d, long = %ld\n", b1, b2, b3, l1);
    fprintf(stdout, "buf1 = %s\n", buf1.buf);
    fprintf(stdout, "ul1 = %lu, ul2 = %lu\n", ul1, ul2);
    fprintf(stdout, "l3 = %ld, l4 = %ld, l5 = %ld\n", l3, l4, l5);
    return 0;
}

static int examples(void)
{
    int r = 0;
    int (*examples[])(void) = {
        example1, example2, example3,
    };

    for (size_t i = 0; i < ELEMENTS(examples); i++) {
        printf("=== === === === Example No %d === === === ===\n", (int)(i + 1));
        if (examples[i]() < 0) {
            r = -1;
        }
    }
    return r;
}

typedef struct {
    bool b1, b2, b3;
    jser_long_t l1, l2, l3;
    jser_ulong_t u1, u2, u3;
    struct array {
        jser_long_t l7, l8;
        char s5[16];
    } array;
    char s1[16], s2[16], s3[16];
    struct nested {
        jser_long_t n4, n5, n6;
        char s4[16];
    } nested;
    jser_buffer_t buf1;
} example_t;

static unsigned char buffer[100] = { 0, 1, 2, 3, 4, 5 };

static example_t example = {
    .b1 = false, .b2 = false, .b3 = true,
    .l1 = 123,   .l2 = -456,  .l3 = 789,
    .u1 = 123,   .u2 =  456,  .u3 = 789,
    .array = {
        .l7 = 1234, .l8 = 0,
        .s5 = "MNO",
    },
    .s1 = "ABC", .s2 = "DEF", .s3 = "",
    .nested = {
        .n4 = 0, .n5 = 1, .n6 = 2,
        .s4 = "XYZ",
    },
    .buf1 = {
        .length = sizeof buffer,
        .used   = 6,
        .buf    = buffer,
    },
};

int print_example(example_t *e, FILE *o)
{
    assert(e);
    assert(o);
    if (fprintf(o, "b1=%d b2=%d b3=%d\n", e->b1, e->b2, e->b3) < 0) {
        return -1;
    }
    if (fprintf(o, "l1=%ld l2=%ld l3=%ld\n", e->l1, e->l2, e->l3) < 0) {
        return -1;
    }
    if (fprintf(o, "u1=%lu u2=%lu u3=%lu\n", e->u1, e->u2, e->u3) < 0) {
        return -1;
    }
    return 0;
}

static int serdes(example_t *e, FILE *o, char *json, size_t length, int serialize)
{
    assert(e);
    assert(o);
    assert(json);

    jser_t array[] = {
        {  .type  =  JSER_LONG_E,    .data.ld      =  &e->array.l7,  },
        {  .type  =  JSER_LONG_E,    .data.ld      =  &e->array.l8,  },
        {  .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  e->array.s5,   },
    };

    jser_t nested[] = {
        {  .attr  =  "n4",  .type  =  JSER_LONG_E,    .data.ld      =  &e->nested.n4,  },
        {  .attr  =  "n5",  .type  =  JSER_LONG_E,    .data.ld      =  &e->nested.n5,  },
        {  .attr  =  "n6",  .type  =  JSER_LONG_E,    .data.ld      =  &e->nested.n6,  },
        {  .attr  =  "s4",  .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  e->nested.s4,   },
    };

    jser_t config[] = {
        {  .attr  =  "b1",    .type  =  JSER_BOOL_E,    .data.b       =  &e->b1,     },
        {  .attr  =  "b2",    .type  =  JSER_BOOL_E,    .data.b       =  &e->b2,     },
        {  .attr  =  "b3",    .type  =  JSER_BOOL_E,    .data.b       =  &e->b3,     },
        {  .attr  =  "l1",    .type  =  JSER_LONG_E,    .data.ld      =  &e->l1,     },
        {  .attr  =  "l2",    .type  =  JSER_LONG_E,    .data.ld      =  &e->l2,     },
        {  .attr  =  "l3",    .type  =  JSER_LONG_E,    .data.ld      =  &e->l3,     },
        {  .attr  =  "a1",    .type  =  JSER_ARRAY_E,   .data.array   =  array,      .length  =  ELEMENTS(array),   .used  =  ELEMENTS(array),   },
        {  .attr  =  "s1",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  &e->s1[0],  },
        {  .attr  =  "s2",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  e->s2,      },
        {  .attr  =  "s3",    .type  =  JSER_ASCIIZ_E,  .data.asciiz  =  e->s3,      },
        {  .attr  =  "j1",    .type  =  JSER_OBJECT_E,  .data.jser    =  nested,     .length  =  ELEMENTS(nested),  .used  =  ELEMENTS(nested),  },
        {  .attr  =  "buf1",  .type  =  JSER_BUFFER_E,  .data.buf     =  &e->buf1,   },
    };

    if (serialize) {
        if (jser_serialize_to_asciiz(config, ELEMENTS(config), 1, json, length) < 0) {
            return -1;
        }
        fprintf(o, "original: %s\n", json);
        return 0;
    }

    jsmntok_t tokens[64];
    if (jser_deserialize_from_asciiz(config, ELEMENTS(config), tokens, ELEMENTS(tokens), json) < 0) {
        return -1;
    }
    fprintf(o, "changed:\n");
    if (print_example(e, o) < 0) {
        return -1;
    }
    return 0;
}

static int usage(FILE *o, const char *arg0)
{
    assert(o);
    assert(arg0);
    unsigned long version = 0;
    const int vset = jser_version(&version);
    const int options = (version >> 24) & 0xFFu;
    char vstr[64] = { 0 };
    static const char *help = "\
Author:  Richard James Howe\n\
License: MIT (for the JSMN library)\n\
\n\
This is a simple test driver program for the JSER library, its only\n\
purposes is to run tests against the library. This test program\n\
includes; serializing and deserializing a structure to file, running\n\
some examples written in C, finding a node within some example JSON\n\
and running a series of built in self tests that are present in the\n\
library.\n\n\
Options:\n\n\
--\tstop processing command line options\n\
-h\tprint this help and exit\n\
-s\trun the serialization config example\n\
-e\trun some examples\n\
-t\trun the libraries internal tests and return pass (0) or failure\n\
-x path\tsearch for node within example configuration\n\
file\tread in JSON for the deserialization config example\n\
\n\
Non-zero is returned on failure, zero on success.\n\n\
";
    if (vset < 0) {
        sprintf(vstr, "ERROR");
    } else {
        sprintf(vstr, "%d.%d.%d", (int)((version >> 16) & 0xFFu), (int)((version >> 8) & 0xFFu), (int)((version >> 0) & 0xFFu));
    }
    return fprintf(o, "Usage: %s\nVersion: %s\nOptions: 0x%x\n%s\n", arg0, vstr, options, help);
}

int main(int argc, char **argv)
{
    int r = 0, no_opt = 0;
    static char json[2048] = { 0 };

    for (int i = 1; i < argc; i++) {
        char *opt = argv[i];
        if (!no_opt && opt[0] == '-') {
            for (int j = 1, ch = 0; (ch = opt[j]); j++) {
                switch (ch) {
                case 's':
                    if (serdes(&example, stdout, json, sizeof json, 1) < 0) {
                        fprintf(stderr, "serialization failed\n");
                        return 1;
                    }
                    break;
                case '-': no_opt    = 1; break;
                case 'h':
                    if (usage(stdout, argv[0]) < 0) {
                        return -1;
                    }
                    return 0;
                case 'e': return examples();
                case 't':
                    if (jser_tests() < 0) {
                        fprintf(stdout, "jser internal tests failed!\n");
                        return 1;
                    }
                    return 0;

                case 'x':
                    break;
                default:
                    (void)usage(stderr, argv[0]);
                    return 1;
                }
            }
        } else {
            errno = 0;
            FILE *f = fopen(argv[i], "rb");
            if (!f) {
                fprintf(stderr, "failed to open file for reading: %s\n", strerror(errno));
                return 1;
            }
            fread(json, 1, sizeof (json) - 1, f);
            if (serdes(&example, stdout, json, sizeof json, 0) < 0) {
                fprintf(stderr, "deserialize failed\n");
                return 1;
            }
            if (fclose(f) < 0) {
                return 1;
            }
        }
    }

    return r;
}

