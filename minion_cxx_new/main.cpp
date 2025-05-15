#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <time.h>
#include <cstdlib>

using namespace minion;

struct Value
{
    short type;
    short flags;
    int size;
    union {
        char* s;
        Value* l;
        int* m;
    };

    /*
    Value()
        : type{0}
        , flags{0}
        , size{0}
        , s{nullptr}
    {}
    */
};

int main()
{
    Value v;
    printf("?? %d %d %d %lu\n", v.type, v.flags, v.size, v.m);
    printf("$ %lu\n", sizeof(v));

    auto v1 = v;
    v1.type = 1;
    auto v2{v};
    v2.flags = 2;
    Value v3(v);
    v3.size = 3;
    Value v4{4, 4, 4, {nullptr}};
    printf("?? %d %d %d %lu\n", v1.type, v1.flags, v1.size, v1.m);
    printf("?? %d %d %d %lu\n", v2.type, v2.flags, v2.size, v2.m);
    printf("?? %d %d %d %lu\n", v3.type, v3.flags, v3.size, v3.m);
    printf("?? %d %d %d %lu\n", v4.type, v4.flags, v4.size, v4.m);

    exit(0);

    const char* fp = "../data/test4.minion";
    const char* f = read_file(fp);
    if (!f) {
        printf("File not found: %s\n", fp);
		exit(1);
	}

    struct timespec start, end;

    Minion miniondata;
    MinionValue parsed;

    for (int count = 0; count < 10; ++count) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

        parsed = miniondata.read(f);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time
        double elapsed = end.tv_sec - start.tv_sec;
        elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
        printf("%0.2f microseconds elapsed\n", elapsed);
        delete (&parsed);
    }

    try {
        parsed = miniondata.read(f);

        char* result = miniondata.dump(parsed, 0);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");
    } catch (MinionError &e) {
        printf("ERROR: %s\n", e.what());
    }
    return 0;
}
