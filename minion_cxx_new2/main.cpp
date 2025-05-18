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
    /*
    std::vector<std::unique_ptr<S>> vec;
    for (int i = 0; i < 4; ++i) {
        vec.emplace_back(std::make_unique<S>("A", i, i * 5));
        //vec.emplace_back(new S{"A", i, i * 5});
    }
    S* w = vec.at(1).get();
    printf("vec[1]: %d %d\n", w->one, w->two);
    vec.resize(2);
    printf("Exiting %zu\n", vec.capacity());
    exit(0);

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

    MinionValue m0;
    printf("m0: %zu\n", m0.index());
    std::string A{"A"};
    std::string B{"B"};
    auto m1 = MinionValue{A};
    auto m2 = MinionValue{B};
    auto ml = MinionValue{MinionList({&m1, &m2})};
    printf("AB: %lu %lu\n", sizeof(m1), sizeof(ml));
    auto mlx = std::get_if<MinionList>(&ml)->at(0);
    printf("A: %s\n", std::get_if<std::string>(mlx)->c_str());
    exit(0);
    */

    const char* fp = "../data/test4.minion";
    const char* f = read_file(fp);
    if (!f) {
        printf("File not found: %s\n", fp);
		exit(1);
	}

    std::string indata{f};

    struct timespec start, end;

    InputBuffer miniondata;

    /*
    for (int count = 0; count < 10; ++count) {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); // Initial timestamp

        miniondata.read(indata);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); // Get current time
        double elapsed = end.tv_sec - start.tv_sec;
        elapsed += (end.tv_nsec - start.tv_nsec) / 1000.0;
        printf("%0.2f microseconds elapsed\n", elapsed);
    }
*/
    try {
        auto m = miniondata.read(indata);

        //TODO++
        DumpBuffer dump_buffer;
        const char* result = dump_buffer.dump(*m, 2);
        if (result)
            printf("\n -->\n%s\n", result);
        else
            printf("*** Dump failed\n");

    } catch (MinionError& e) {
        printf("ERROR: %s\n", e.what());
    }
    return 0;
}
