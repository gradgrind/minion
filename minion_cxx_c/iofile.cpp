#include "iofile.h"
#include <fstream>

using namespace std;

string filedata;

bool readfile(
    string &data, const string &filepath)
{
    std::ifstream file(filepath);
    if (file) {
        data.assign((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
        return true;
    }
    return false;
}

const char *read_file(
    const char *filepath)
{
	auto f = string{filepath};
	if (readfile(filedata, f)) return filedata.c_str();
	else return 0;
}

bool writefile(
    const string &data, const string &filepath)
{
    std::ofstream file(filepath);
    if (file) {
        file << data;
        file.close();
        return true;
    }
    return false;
}
