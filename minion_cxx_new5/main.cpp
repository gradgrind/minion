#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <cstdlib>
#include <time.h>

using namespace minion;


int main()
{
    auto fplist = {"../data/test1.minion",
                   "../data/test2.minion",
                   "../data/test3.minion",
                   "../data/test4.minion"};

    std::string indata;

    struct timespec start, end, xtra;

    InputBuffer miniondata;

    for (int count = 0; count < 10; ++count) {
        for (const auto& fp : fplist) {
            const char* f = read_file(fp);
            if (!f) {
                printf("File not found: %s\n", fp);
                exit(1);
            }
            indata = f;

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

            auto m = miniondata.read(indata);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time

            delete_mvalue(m);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &xtra); // Get current time

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
        auto m = miniondata.read(indata);

        DumpBuffer dump_buffer;
        const char* result = dump_buffer.dump(m, 2);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");

    } catch (MinionError& e) {
        printf("ERROR: %s\n", e.what());
    }
    return 0;
}
