#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <time.h>
#include <cstdlib>

using namespace minion;

int main()
{
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
