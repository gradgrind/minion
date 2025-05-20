#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <cstdlib>
#include <time.h>

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

struct S
{
    std::string name;
    int one;
    int two;

    ~S() { printf("-- deleting %d\n", one); }
};

int main()
{
    auto fplist = {"../data/test1.minion",
                   "../data/test2.minion",
                   "../data/test3.minion",
                   "../data/test4.minion"};

    std::string indata;

    struct timespec start, end, xtra;

    Minion miniondata;
    MinionValue* parsed;

    for (int count = 0; count < 20000; ++count) {
        for (const auto& fp : fplist) {
            const char* f = read_file(fp);
            if (!f) {
                printf("File not found: %s\n", fp);
                exit(1);
            }
            indata = f;

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

            parsed = miniondata.read(f);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time

            miniondata.clear_data();

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &xtra); // Get current

            double elapsed = end.tv_sec - start.tv_sec;
            elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
            printf("%0.2f microseconds elapsed\n", elapsed);

            elapsed = xtra.tv_sec - end.tv_sec;
            elapsed += (xtra.tv_nsec - end.tv_nsec) / 1000.0;
            printf("%0.2f microseconds freeing\n", elapsed);
        }
        printf("  - - - - -\n");
    }

    try {
        parsed = miniondata.read(indata);

        //TODO++
        const char* result = miniondata.dump(parsed, 2);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");

    } catch (MinionError& e) {
        printf("ERROR: %s\n", e.what());
    }
    return 0;
}
