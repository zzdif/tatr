#include "../thirdparty/sha256.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

static void expect_sha256(const void *data, size_t length, const char *expected)
{
    char actual[72];
    memset(actual, 'x', sizeof(actual));
    scheduler_sha256(data, length, actual);
    assert(strncmp(actual, "sha256:", 7) == 0);
    assert(strcmp(actual + 7, expected) == 0);
    assert(actual[71] == '\0');
}

int main(void)
{
    /* RFC 6234, section 8.5: tests 1 and 3; empty is a standard KAT. */
    expect_sha256("", 0,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect_sha256("abc", 3,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    expect_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    static char multiblock[1000000];
    memset(multiblock, 'a', sizeof(multiblock));
    expect_sha256(multiblock, sizeof(multiblock),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    puts("PASS: RFC SHA-256 empty, abc, 56-byte padding boundary, million-a vectors and fingerprint prefix");
    return 0;
}
