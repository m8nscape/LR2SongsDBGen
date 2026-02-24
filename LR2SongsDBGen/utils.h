#pragma once
#include <filesystem>
#include <iostream>
#include <shared_mutex>
#include <list>
#include <tchar.h>
#include <WinNls.h>

void initWin32API();
void releaseWin32API();
bool makeLR2PathKey(const char* folderpath, int pathLen, char* output);
int makeLR2Date(int tid, const std::filesystem::path& filepath);
bool md5Content(const std::string& content, char* output);
std::vector<std::filesystem::path> dir(const std::filesystem::path& root);

std::string w2cp(const std::wstring& w, int cp = CP_UTF8);
std::wstring cp2w(const std::string& w, int cp = CP_UTF8);

#define MAX_THREADS 32


template <typename T>
class TSList
{
private:
    mutable std::shared_mutex lock;
    std::list<T> list;
public:
    T top() const
    {
        T tmp{};
        {
            std::shared_lock l(lock);
            if (!list.empty())
                tmp = list.front();
        }
        return tmp;
    }
    void pop_front()
    {
        std::unique_lock l(lock);
        return list.pop_front();
    }
    void push_back(const T& v)
    {
        std::unique_lock l(lock);
        return list.push_back(v);
    }
    bool empty() const
    {
        std::shared_lock l(lock);
        return list.empty();
    }
    size_t size() const
    {
        std::shared_lock l(lock);
        return list.size();
    }
};
