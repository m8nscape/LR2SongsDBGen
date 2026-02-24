#pragma once
#include <string_view>
#include <filesystem>

bool initDB(const std::filesystem::path& path);
void releaseDB();

void insertFolder(
    const std::string_view& title,
    const std::string_view& path,
    const int type, 
    const std::string_view& parent,
    const int date,
    const int adddate
);

void insertSong(
    const std::string_view& hash,
    const std::string_view& title,
    const std::string_view& subtitle,
    const std::string_view& genre,
    const std::string_view& artist,
    const std::string_view& subartist,
    const std::string_view& path,
    const std::string_view& folder,
    const std::string_view& stagefile,
    const std::string_view& banner,
    const std::string_view& backbmp,
    const std::string_view& parent,
    const int level,
    const int difficulty,
    const int maxbpm,
    const int minbpm,
    const int mode,
    const int judge,
    const int longnotes,
    const int bga,
    const int random,
    const int date,
    const int txt,
    const int karinotes,
    const int adddate,
    const int exlevel
);
