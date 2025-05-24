#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <cstdlib>
#include <time.h>

using namespace minion;

std::vector<MPair> data
    = {{"A", {}}, {"B", {}}, {"C", {}}, {"D", {}}, {"E", {}}, {"F", {}}, {"G", {}}, {"H", {}}};

int get1(
    std::string_view key)
{
    int s = data.size();
    for (int i = 0; i < s; ++i) {
        if (data.at(i).first == key)
            return i;
    }
    return -1;
}

int get2(
    std::string_view key)
{
    int i = 0;
    for (auto& mp : data) {
        if (mp.first == key)
            return i;
        ++i;
    }
    return -1;
}

int main()
{
    auto fplist = {
        "../data/test1.minion",
        //"../data/test1a.minion",
        "../data/test2.minion",
        //"../data/test2a.minion",
        //"../data/test2e.minion",
        "../data/test3.minion",
        "../data/test4.minion",
        //"../data/test4e.minion"
        //
    };

    std::string indata;

    struct timespec start, end, xtra;

    InputBuffer miniondata;

    MinionValue m;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp
    int c = 0;
    for (int count = 0; count < 1000000; ++count) {
        c += get1("X");
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time
    int d = 0;
    for (int count = 0; count < 1000000; ++count) {
        d += get2("X");
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &xtra); // Get current time

    printf("§§§ %d %d\n", c, d);

    double elapsed = end.tv_sec - start.tv_sec;
    elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
    printf("%0.2f microseconds get1\n", elapsed);

    elapsed = xtra.tv_sec - end.tv_sec;
    elapsed += (xtra.tv_nsec - end.tv_nsec) / 1000.0;
    printf("%0.2f microseconds get2\n", elapsed);
    exit(0);

    for (int count = 0; count < 10; ++count) {
        for (const auto& fp : fplist) {
            const char* f = read_file(fp);
            if (!f) {
                printf("File not found: %s\n", fp);
                exit(1);
            }
            indata = f;

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

            miniondata.read(m, indata);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time

            m = {};

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

    auto perror = miniondata.read(m, indata);
    if (perror) {
        printf("PARSE ERROR: %s\n", perror);
    } else if (!m.is_null()) {
        DumpBuffer dump_buffer;
        const char* result = dump_buffer.dump(m, 2);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");
    }

    return 0;
}
