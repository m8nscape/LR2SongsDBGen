#pragma once
#include <vector>
#include <string>
#include <filesystem>

// LR2 gracefully stores its config.xml in UTF-8
std::vector<std::string> getJukebox(const std::filesystem::path& xml);
