#pragma once
// Stands in for FPP's settings.h in tools/host. FPP_DIR_MEDIA is FPP's media
// folder; the host maps it to this checkout and its own data folder (host.cpp).

#include <string>

std::string hostMediaPath(const std::string &sub);
#define FPP_DIR_MEDIA(sub) hostMediaPath(sub)
