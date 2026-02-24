#include "db.h"
#include "db_conn.h"
#include <iostream>

SQLite* db;

bool initDB(const std::filesystem::path& path)
{
    auto p = path;
    if (!std::filesystem::exists(p))
    {
        std::cout << "song.db not found. Run LR2 once first" << std::endl;
        std::cout << std::endl;
        return false;
    }

    int idx = 1;
    p = std::filesystem::path(path.string() + ".bak");
    while (std::filesystem::exists(p))
    {
        p = std::filesystem::path(path.string() + ".bak" + std::to_string(idx++));
    }
    std::filesystem::copy(path, p);

    db = new SQLite(path.string().c_str(), "song.db");

    db->exec("DROP TABLE IF EXISTS folder");
    db->exec("DROP TABLE IF EXISTS song");

    db->exec("CREATE TABLE song(hash TEXT ,title TEXT ,subtitle TEXT ,genre TEXT,artist TEXT,subartist TEXT,tag TEXT ,path TEXT primary key ,type INTEGER,folder TEXT,stagefile TEXT,banner TEXT,backbmp TEXT,parent TEXT,level INTEGER,difficulty INTEGER,maxbpm INTEGER,minbpm INTEGER,mode INTEGER,judge INTEGER,longnote INTEGER,bga INTEGER,random INTEGER,date INTEGER,favorite INTEGER,txt INTEGER,karinotes INTEGER,adddate INTEGER,exlevel INTEGER);");
    db->exec("CREATE TABLE folder(title TEXT ,subtitle TEXT ,category TEXT,info_a TEXT,info_b TEXT,command TEXT,path TEXT primary key,type INTEGER,banner TEXT,parent TEXT,date INTEGER,max INTEGER,adddate INTEGER);");

    return true;
}

void releaseDB()
{
    if (db)
    {
        delete db;
    }
}

void insertFolder(
    const std::string_view& title,
    const std::string_view& path,
    const int type,
    const std::string_view& parent,
    const int date,
    const int adddate
)
{
    db->exec("insert into folder(title,path,type,parent,date,adddate) values (?,?,?,?,?,?)",
        {
            title,
            path,
            type,
            parent,
            date,
            adddate
        });
}

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
    const int longnote,
    const int bga,
    const int random,
    const int date,
    const int txt,
    const int karinotes,
    const int adddate,
    const int exlevel
)
{
    db->exec("insert into song(hash,title,subtitle,genre,artist,subartist,path,type,folder,stagefile,banner,backbmp,parent,level,difficulty,maxbpm,minbpm,mode,judge,longnote,bga,random,date,favorite,txt,karinotes,adddate,exlevel) values (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        { 
            hash,
            title,
            subtitle,
            genre,
            artist,
            subartist,
            path,
            0,
            folder,
            stagefile,
            banner,
            backbmp,
            parent,
            level,
            difficulty,
            maxbpm,
            minbpm,
            mode,
            judge,
            longnote,
            bga,
            random,
            date,
            0,
            txt,
            karinotes,
            adddate,
            exlevel
        });
}