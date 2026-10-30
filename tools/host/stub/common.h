#pragma once
// Stands in for FPP's common.h in tools/host: the file helpers the plugin uses.

#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include "HostJson.h"

inline bool FileExists(const std::string &f) {
    struct stat st;
    return stat(f.c_str(), &st) == 0;
}

inline bool LoadJsonFromFile(const std::string &f, Json::Value &root) {
    std::ifstream in(f);
    if (!in) {
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return Json::parse(ss.str(), root);
}

inline bool SaveJsonToFile(const Json::Value &root, const std::string &f, const std::string &indent = "\t") {
    std::ofstream out(f);
    out << Json::write(root, indent) << "\n";
    return (bool)out;
}
