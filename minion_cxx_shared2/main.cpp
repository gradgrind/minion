#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <cstdlib>
#include <time.h>

using namespace minion;

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

    MValue m;

    for (int count = 0; count < 10; ++count) {
        for (const auto& fp : fplist) {
            const char* f = read_file(fp);
            if (!f) {
                printf("File not found: %s\n", fp);
                exit(1);
            }
            indata = f;

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

            m = Reader::read(indata);

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time

            Writer w(m, -1);
            m = {}; // free memory

            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &xtra); // Get current time

            double elapsed = end.tv_sec - start.tv_sec;
            elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
            printf("%0.2f microseconds elapsed\n", elapsed);

            elapsed = xtra.tv_sec - end.tv_sec;
            elapsed += (xtra.tv_nsec - end.tv_nsec) / 1000.0;
            printf("%0.2f microseconds dumping\n", elapsed);
        }
        printf("  - - - - -\n");
    }

    m = Reader::read(indata);
    if (m.type() == T_Error) {
        printf("PARSE ERROR: %s\n", m.error_message());
    } else if (!m.is_null()) {
        Writer writer(m, 0);
        const char* result = writer.dump_c();
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");
    }

    return 0;
}
