#define NOMINMAX

#include "utils.h"
#include "cfg.h"
#include "db.h"
#include <filesystem>
#include <string>
#include <iostream>
#include <thread>
#include <list>
#include <utility>
#include <cwctype>
#include <sstream>
#include <fstream>
#include <shared_mutex>
#include <algorithm>
#include <charconv>
#include <execution>
#include <functional>
#include <stringapiset.h>
#include <re2/re2.h>
namespace fs = std::filesystem;

void worker(int tid);
TSList<fs::path> tasks[MAX_THREADS];

#define RDBUF_SIZE (8*1024)
char rdbuf[MAX_THREADS][RDBUF_SIZE];

struct BmsFile
{
    std::string pathKey;
    fs::path path;
    std::string parent;
    bool pms = false;
    bool txt = false;
    int modifyDate = 0;
    std::string bytes;
};

struct workerTask
{
    TSList<BmsFile*> tasks;
};

void workerMain(int id);

std::pair<std::string, std::string> splitSubTitle(const std::string& title)
{
    static const LazyRE2 subTitleRegex[]
    {
        { R"((.+) *(-.*?-))" },
        { R"((.+) *(〜.*?〜))" },
        { R"((.+) *(\(.*?\)))" },
        { R"((.+) *(\[.*?\]))" },
        { R"((.+) *(<.*?>))" },
    };
    for (auto& reg : subTitleRegex)
    {
        std::string t1, t2;
        if (RE2::FullMatch(title, *reg, &t1, &t2))
        {
            return { t1, t2 };
        }
    }
    return { title, "" };
}

////////////////////////////////////////////////////////////////////////////////

std::string sjis_to_utf8(const std::string& input)
{
    return w2cp(cp2w(input, 932), CP_UTF8);
}


// string to int
int toInt(std::string_view str, int defVal = 0) noexcept
{
    int val = 0;
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc())
        return val;
    else
        return defVal;
}
int toInt(const std::string& str, int defVal) noexcept { return toInt(std::string_view(str)); }

// string to double
double toDouble(std::string_view str, double defVal = 0.) noexcept
{
    double val = 0;
    if (auto [p, ec] = std::from_chars(str.data(), str.data() + str.size(), val); ec == std::errc())
        return val;
    else
        return defVal;
}
double toDouble(const std::string& str, double defVal) noexcept { return toDouble(std::string_view(str), defVal); }

// strcasecmp
bool strEqual(std::string_view str1, std::string_view str2, bool icase) noexcept
{
    if (icase)
    {
        return std::equal(std::execution::seq, str1.begin(), str1.end(), str2.begin(), str2.end(),
            [](char c1, char c2) { return std::tolower(c1) == std::tolower(c2); });
    }
    else
    {
        return str1 == str2;
    }
}
bool strEqual(const std::string& str1, std::string_view str2, bool icase) noexcept { return strEqual(std::string_view(str1), str2, icase); }

constexpr unsigned base36(char c)
{
    return (c > '9') ? (c >= 'a' ? 10 + c - 'a' : 10 + c - 'A') : (c - '0');
}

constexpr unsigned base36(char first, char second)
{
    return 36 * base36(first) + base36(second);
}

constexpr unsigned base36(const char* c)
{
    return base36(c[0], c[1]);
}

constexpr unsigned base16(char c)
{
    return (c > '9') ? (c >= 'a' ? 10 + c - 'a' : 10 + c - 'A') : (c - '0');
}

constexpr unsigned base16(char first, char second)
{
    return 16 * base16(first) + base16(second);
}

constexpr unsigned base16(const char* c)
{
    return base16(c[0], c[1]);
}

////////////////////////////////////////////////////////////////////////////////

workerTask workerTasks[MAX_THREADS];
bool isReadDiskFinished = false;
bool isTasksFinished = false;

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    system("chcp 65001"); 

    if (argv[1] == std::string("-r"))
    {
        std::string s = argv[2];
        auto [t1, t2] = splitSubTitle(s);
        std::cout << "Title: " << t1 << std::endl;
        std::cout << "SubTitle: " << t2 << std::endl;
        system("pause");
        return 0;
    }

    if (argc != 2)
    {
        std::cout << "Usage: LR2SongsDBGen.exe [LR2.exe]" << std::endl;
        std::cout << std::endl;
        system("pause");
        return 1;
    }

    fs::path lr2Exe = argv[1];
    if (!fs::exists(lr2Exe) ||
        !fs::is_regular_file(lr2Exe))
    {
        std::cout << "Usage: LR2SongsDBGen.exe [LR2.exe]" << std::endl;
        std::cout << std::endl;
        system("pause");
        return 1;
    }

    bool HD = false;
    if (lr2Exe.filename() == "LR2_HD.exe")
    {
        HD = true;
    }
    fs::path lr2Path = lr2Exe.parent_path();
    if (!fs::exists(lr2Path) ||
        !fs::is_directory(lr2Path) ||
        !fs::exists(lr2Path / "LR2files") ||
        !fs::is_directory(lr2Path / "LR2files"))
    {
        std::cout << "The path you have specified is not a LR2 installation." << std::endl;
        std::cout << std::endl;
        system("pause");
        return 1;
    }

    fs::path xmlPath = lr2Path / "LR2files" / "Config" / (HD ? "config.xmh" : "config.xml");
    if (!fs::exists(lr2Path))
    {
        std::cout << "Config file not found. Run LR2 once first." << std::endl;
        std::cout << std::endl;
        system("pause");
        return 1;
    }

    initWin32API();
    if (!initDB(lr2Path / "LR2files" / "Database" / "song.db"))
    {
        releaseWin32API();
        return 1;
    }

    std::filesystem::current_path(lr2Path);

    auto jukebox = getJukebox(xmlPath);

    int codepage = CP_ACP;
    bool codepageConfirmed = false;
    std::vector<std::wstring> jukeboxW;
    while (!codepageConfirmed)
    {
        jukeboxW.clear();
        for (auto& rootANSI : jukebox)
        {
            jukeboxW.push_back(cp2w(rootANSI, codepage));
        }

        std::cout << std::endl << std::endl;

        std::cout << "Codepage: " << codepage << std::endl;

        std::cout << "Your jukebox paths: (Change codepage if there is any garbage)" << std::endl;
        for (auto& rootw : jukeboxW)
        {
            std::cout << " - " << w2cp(rootw) << std::endl;
        }

        std::cout << "Change codepage? (y/n) [n] ";
        char c = _getwche();
        std::cout << std::endl;
        switch (c)
        {
        case 'y':
        case 'Y':
            std::cout << "Choose: (0) System (ANSI) / (1) 932 (Shift-JIS) / (2) 936 (GBK) / (3) 949 (Korean) ";
            c = _getwche();
            switch (c)
            {
            case '0':
                codepage = CP_ACP;
                break;
            case '1':
                codepage = 932;
                break;
            case '2':
                codepage = 936;
                break;
            case '3':
                codepage = 949;
                break;
            }
            break;
        case 'n':
        case 'N':
        case '\r':
        case '\n':
            codepageConfirmed = true;
            break;
        }
    }

    int threads = std::min(std::max(2u, std::thread::hardware_concurrency()) - 1, (unsigned)MAX_THREADS);

    TSList<std::pair<fs::path, std::string>> pendingDirs;
    TSList<BmsFile*> pendingFiles;

    auto ioThread = std::thread([&] {
        for (auto& rootw : jukeboxW)
        {
            auto walkDir = [&](fs::path root, const std::string& parent)
            {
                if (root.empty())
                    return;

                if (root.native().back() != L'\\')
                    root = root.native() + L"\\";

                std::string rootUtf8 = w2cp(root.native());

                std::cout << "*** Entering " << rootUtf8 << std::endl;
                if (!fs::exists(root))
                {
                    std::cout << "*** " << rootUtf8 << " does not exist" << std::endl;
                    return;
                }

                std::string pathKey(8, '\0');
                makeLR2PathKey(rootUtf8.c_str(), rootUtf8.length(), pathKey.data());

                std::string nameUtf8 = w2cp(root.parent_path().filename().native());

                int rootModifyDate = makeLR2Date(0, root);
                int currentDate = makeLR2Date(0, "");

                insertFolder(nameUtf8, rootUtf8, 1, parent, rootModifyDate, currentDate);


                bool hasChart = false;
                bool hasReadme = false;
                std::list<fs::path> subDirs;
                std::list<BmsFile*> subFiles;


                std::vector<fs::path> dir = ::dir(root);
                std::sort(dir.begin(), dir.end());

                for (auto& p : dir)
                {
                    if (fs::is_directory(p))
                    {
                        subDirs.push_back(p);
                    }
                    else if (p.has_extension())
                    {
                        auto ext = p.extension().native();
                        std::transform(ext.begin(), ext.end(), ext.begin(), std::towlower);
                        if (ext == L".bms" || ext == L".bme" || ext == L".bml" || ext == L".pms")
                        {
                            auto f = new BmsFile();

                            std::ifstream ifs(p.native(), std::ios::binary);
                            if (ifs.fail())
                            {
                                std::cout << w2cp(p.native()) << ": Open failed" << std::endl;
                                continue;
                            }

                            ifs.seekg(0, std::ios_base::end);
                            auto iSize = ifs.tellg();
                            ifs.seekg(0, std::ios_base::beg);
                            f->bytes.resize(iSize);
                            ifs.read(f->bytes.data(), iSize);
                            ifs.close();

                            f->pathKey = pathKey;
                            f->path = p;
                            f->parent = parent;
                            f->pms = (ext == L".pms");
                            f->modifyDate = makeLR2Date(0, p);

                            hasChart = true;
                            subFiles.push_back(f);
                        }
                        else if (ext == L".txt")
                        {
                            hasReadme = true;
                        }
                    }
                }

                if (hasChart)
                {
                    for (auto& file : subFiles)
                    {
                        file->txt = hasReadme;
                        if (pendingFiles.size() > 64)
                        {
                            // too much bms stalling
                            while (pendingFiles.size() > 16)
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            }
                        }
                        pendingFiles.push_back(file);
                    }
                }
                else
                {
                    for (auto& dir : subDirs)
                        pendingDirs.push_back(std::make_pair(dir, pathKey));
                }
            };

            walkDir(fs::path(rootw), "e2977170");
            while (!pendingDirs.empty())
            {
                auto p = pendingDirs.top();
                pendingDirs.pop_front();
                walkDir(p.first, p.second);
            }
        }

        isReadDiskFinished = true;
        });

    int workerId = 0;
    auto dispatchThread = std::thread([&] {
        while (!isReadDiskFinished || !pendingFiles.empty())
        {
            if (pendingFiles.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            auto f = pendingFiles.top();
            pendingFiles.pop_front();
            workerTasks[workerId++ % threads].tasks.push_back(f);
        }
        isTasksFinished = true;
        });

    std::vector<std::thread> workers;
    for (int i = 0; i < threads; i++)
    {
        workers.push_back(std::thread(std::bind(workerMain, i)));
    }

    for (int i = 0; i < threads; i++)
    {
        workers[i].join();
    }

    ioThread.join();
    dispatchThread.join();

    std::cout << "Done." << std::endl;
    std::cout << std::endl;

    releaseDB();
    releaseWin32API();

    system("pause");
    return 0;
}

void workerMain(int id)
{
    auto& tasks = workerTasks[id];
    while (!isTasksFinished || !tasks.tasks.empty())
    {
        if (tasks.tasks.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        auto f = tasks.tasks.top();
        tasks.tasks.pop_front();

        int currentDate = makeLR2Date(id, "");
        std::string hash(32, '\0');
        if (!md5Content(f->bytes, hash.data()))
        {
            std::cout << w2cp(f->path.native()) << ": hash failed" << std::endl;
            continue;
        }

        std::string pathUtf8 = w2cp(f->path.native());

        std::string title;
        std::string title2;
        std::string artist;
        std::string artist2;
        std::string genre;
        std::string version;     // mostly known as difficulty name
        std::string stagefile;
        std::string banner;
        std::string backbmp;
        int level = 0;
        int difficulty = 2;
        int mode = 5;
        int judge = 2;  // default NORMAL
        int maxbpm = 150;
        int minbpm = 150;
        int longnote = 0;
        int bga = 0;
        int random = 0;
        int karinotes = 0;

        bool hasDifficulty = false;
        bool hasBpm = false;
        bool has7k = false;
        bool hasDP = false;

        int lnobj = -1;
        int noteCount = 0;
        int lnobjCount = 0;     // #LNOBJ
        int lnNoteCount = 0;    // #xxx5y, #xxx6y
        std::map<int, double> exBPM;

        std::stringstream ss(f->bytes);
        ss.rdbuf()->pubsetbuf(rdbuf[id], RDBUF_SIZE);
        while (!ss.eof())
        {
            std::string buf;
            std::getline(ss, buf);
            buf = sjis_to_utf8(buf);
            buf.resize(std::min(buf.length(), buf.rfind('\r')));

            static LazyRE2 regexNote{ R"(#[\d]{3}[0-9A-Za-z]{2}:.*)" };
            if (!RE2::FullMatch(re2::StringPiece(buf.data(), buf.length()), *regexNote))
            {
                auto spacePos = std::min(buf.length(), buf.find_first_of(' '));
                if (spacePos <= 1) continue;

                std::string key = buf.substr(1, spacePos - 1);
                std::string value = spacePos < buf.length() ? buf.substr(spacePos + 1) : "";

                static LazyRE2 regexBga{ R"((?i)BMP[0-9A-Za-z]{1,2})" };
                static LazyRE2 regexBpm{ R"((?i)BPM[0-9A-Za-z]{1,2})" };

                if (strEqual(key, "TITLE", true))
                    title.assign(value.begin(), value.end());
                else if (strEqual(key, "SUBTITLE", true))
                    title2.assign(value.begin(), value.end());
                else if (strEqual(key, "ARTIST", true))
                    artist.assign(value.begin(), value.end());
                else if (strEqual(key, "SUBARTIST", true))
                    artist2.assign(value.begin(), value.end());
                else if (strEqual(key, "GENRE", true))
                    genre.assign(value.begin(), value.end());
                else if (strEqual(key, "STAGEFILE", true))
                    stagefile.assign(value.begin(), value.end());
                else if (strEqual(key, "BANNER", true))
                    banner.assign(value.begin(), value.end());
                else if (strEqual(key, "BACKBMP", true))
                    backbmp.assign(value.begin(), value.end());
                else if (strEqual(key, "RANDOM", true))
                    random = 1;
                else if (strEqual(key, "BPM", true))
                {
                    int bpm = int(toDouble(value));
                    if (!hasBpm)
                    {
                        hasBpm = true;
                        minbpm = maxbpm = bpm;
                    }
                    else
                    {
                        minbpm = std::min(minbpm, bpm);
                        maxbpm = std::max(maxbpm, bpm);
                    }
                }
                else if (strEqual(key, "PLAYLEVEL", true))
                {
                    level = toInt(value);
                }
                else if (strEqual(key, "DIFFICULTY", true))
                {
                    hasDifficulty = true;
                    difficulty = toInt(value);
                }
                else if (strEqual(key, "RANK", true))
                {
                    judge = toInt(value);
                }
                else if (strEqual(key, "LNOBJ", true) && value.length() >= 2)
                {
                    lnobj = base36(value[0], value[1]);
                }
                else if (RE2::FullMatch(re2::StringPiece(key.data(), key.length()), *regexBga))
                {
                    bga = 1;
                }
                else if (RE2::FullMatch(re2::StringPiece(key.data(), key.length()), *regexBpm))
                {
                    int idx = base36(key[3], key[4]);
                    if (idx != 0)
                        exBPM[idx] = toDouble(value);
                }
            }
            else
            {
                auto colon_idx = buf.find_first_of(':');
                std::string key = buf.substr(1, 5);
                std::string value = buf.substr(7);
                    
                if (value.empty())
                {
                    continue;
                }

                auto parseNotes = [](std::string_view value)
                {
                    std::vector<int> tmp;
                    for (int i = 0; i + 1 < value.length(); i += 2)
                    {
                        tmp.push_back(base36(value[i], value[i + 1]));
                    }
                    return tmp;
                };

                auto parseNotes16 = [](std::string_view value)
                {
                    std::vector<int> tmp;
                    for (int i = 0; i + 1 < value.length(); i += 2)
                    {
                        tmp.push_back(base16(value[i], value[i + 1]));
                    }
                    return tmp;
                };

                int x_ = base36(key[3]);
                int _y = base36(key[4]);

                if (x_ == 0)
                {
                    if (_y == 3)    // BPM
                    {
                        // the value range is [01-FF], do not parse as base36
                        auto notes = parseNotes16(value);
                        for (auto& bpm : notes)
                        {
                            if (bpm == 0)
                                continue;

                            if (!hasBpm)
                            {
                                hasBpm = true;
                                minbpm = maxbpm = bpm;
                            }
                            else
                            {
                                minbpm = std::min(minbpm, bpm);
                                maxbpm = std::max(maxbpm, bpm);
                            }
                        }
                    }
                    else if (_y == 8)    // exBPM
                    {
                        auto notes = parseNotes(value);
                        for (auto& n : notes)
                        {
                            if (n == 0 || exBPM.find(n) == exBPM.end())
                                continue;

                            int bpm = int(exBPM[n]);
                            if (!hasBpm)
                            {
                                hasBpm = true;
                                minbpm = maxbpm = bpm;
                            }
                            else
                            {
                                minbpm = std::min(minbpm, bpm);
                                maxbpm = std::max(maxbpm, bpm);
                            }
                        }
                    }
                }
                else
                {
                    switch (x_)
                    {
                    case 2:
                    case 4:
                    case 6:
                    case 0xE:
                        hasDP = true;
                        break;
                    }

                    if (x_ == 1 || x_ == 2)
                    {
                        if (_y == 8 || _y == 9)
                        {
                            has7k = true;
                        }

                        auto notes = parseNotes(value);
                        for (auto& n : notes)
                        {
                            if (n == 0)
                                continue;

                            if (n == lnobj)
                            {
                                lnobjCount++;
                            }
                            else
                            {
                                noteCount++;
                            }
                        }
                    }
                    else if (x_ == 5 || x_ == 6)
                    {
                        if (_y == 8 || _y == 9)
                        {
                            has7k = true;
                        }

                        auto notes = parseNotes(value);
                        for (auto& n : notes)
                        {
                            if (n == 0)
                                continue;

                            if (n == lnobj)
                            {
                                lnobjCount++;
                            }
                            else
                            {
                                lnNoteCount++;
                            }
                        }
                    }
                }
            }
        }

        // implicit subtitle
        if (title2.empty())
        {
            auto [t1, t2] = splitSubTitle(title);
            title = t1;
            title2 = t2;
        }

        // implicit difficulty
        if (!hasDifficulty)
        {
            static const LazyRE2 difficultyRegex[]
            {
                { "" },
                { R"((?i)(easy|beginner|light))" },
                { R"((?i)(normal|standard))" },
                { R"((?i)(hard|hyper))" },
                { R"((?i)(ex|another|insane|lunatic|maniac))" },
            };
            difficulty = -1;
            for (int i = 4; i >= 1; --i)
            {
                if (RE2::PartialMatch(version, *difficultyRegex[i]))
                {
                    difficulty = i;
                    break;
                }
                if (RE2::PartialMatch(title2, *difficultyRegex[i]))
                {
                    difficulty = i;
                    break;
                }
                if (RE2::PartialMatch(title, *difficultyRegex[i]))
                {
                    difficulty = i;
                    break;
                }
            }
            if (difficulty == -1)
                difficulty = 2; // defaults to normal
        }

        mode = (f->pms ? 9 : ((has7k ? 7 : 5) * (hasDP ? 2 : 1)));
        karinotes = noteCount + lnNoteCount / 2;
        longnote = (lnNoteCount != 0 || lnobjCount != 0) ? 1 : 0;

        insertSong(
            hash,
            title,
            title2,
            genre,
            artist,
            artist2,
            pathUtf8,
            f->pathKey,
            stagefile,
            banner,
            backbmp,
            f->parent,
            level,
            difficulty,
            maxbpm,
            minbpm,
            mode,
            judge,
            longnote,
            bga,
            random,
            f->modifyDate,
            (f->txt ? 1 : 0),
            karinotes,
            currentDate,
            0
        );

        std::cout << w2cp(f->path.native()) << std::endl;

        delete f;
    }
}