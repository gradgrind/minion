#include "iofile.h"
#include "minion.h"
#include <cstdio>
#include <time.h>
#include <cstdlib>

int main()
{
    const char* fp = "../data/test4.minion";
    const char* f = read_file(fp);
    if (!f) {
        printf("File not found: %s\n", fp);
		exit(1);
	}

    struct timespec start, end;
    minion_doc parsed;

    for (int count = 0; count < 10; ++count) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

        parsed = minion_read(f);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time
        double elapsed = end.tv_sec - start.tv_sec;
        elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
        printf("%0.2f microseconds elapsed\n", elapsed);
        minion_free(parsed);
    }

    parsed = minion_read(f);

    char* e = minion_error(parsed);
    if (e) {
        printf("ERROR: %s\n", (char*) e);
    } else {
        printf("Returned type: %d\n", parsed.minion_item.type);

        char* result = minion_dump(parsed.minion_item, 0);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");
    }
    minion_free(parsed);

    minion_tidy(); // free minion buffers
    return 0;
}
