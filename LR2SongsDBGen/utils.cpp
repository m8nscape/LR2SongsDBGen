#include "utils.h"
#include <Windows.h>
#include <bcrypt.h>
#include <string>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

typedef unsigned int(__stdcall* pfnAPI)(int dwInitial, void* pData, int iLen);
HMODULE tMod = NULL;
pfnAPI RtlComputeCrc32 = NULL;

#define NT_SUCCESS(Status)          (((NTSTATUS)(Status)) >= 0)
#define STATUS_UNSUCCESSFUL         ((NTSTATUS)0xC0000001L)
static const BYTE rgbMsg[] =
{
    0x61, 0x62, 0x63
};
BCRYPT_ALG_HANDLE       hAlg = NULL;
DWORD cbData = 0;
DWORD cbHash = 0;

void initWin32API()
{
    tMod = LoadLibraryW(L"ntdll.dll");
    if (tMod == 0)
    {
        std::cout << "ntdll.dll load error" << std::endl;
        system("pause");
        exit(1);
    }
    RtlComputeCrc32 = (pfnAPI)GetProcAddress(tMod, "RtlComputeCrc32");
    if (RtlComputeCrc32 == NULL)
    {
        std::cout << "RtlComputeCrc32 not found" << std::endl;
        system("pause");
        exit(1);
    }

    //open an algorithm handle
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    if (!NT_SUCCESS(status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_MD5_ALGORITHM,
        NULL,
        0)))
    {
        std::cout << "BCryptOpenAlgorithmProvider failed: " << status << std::endl;
        system("pause");
        exit(1);
    }

    //calculate the size of the buffer to hold the hash object
    if (!NT_SUCCESS(status = BCryptGetProperty(
        hAlg,
        BCRYPT_HASH_LENGTH,
        (PBYTE)&cbHash,
        sizeof(DWORD),
        &cbData,
        0)))
    {
        std::cout << "BCryptGetProperty BCRYPT_HASH_LENGTH failed: " << status << std::endl;
        system("pause");
        exit(1);
    }

}

void releaseWin32API()
{
    BCryptCloseAlgorithmProvider(hAlg, 0);
}

bool makeLR2PathKey(const char* folderpath, int pathLen, char* output)
{
    // basically CRC32 but also include the terminating \0
    unsigned crc32 = RtlComputeCrc32(0, (void*)folderpath, pathLen + 1);
    sprintf_s(output, 9, "%02x%02x%02x%02x", 
        (((crc32 & 0xff000000) >> 24) & 0xFF),
        (((crc32 & 0x00ff0000) >> 16) & 0xFF),
        (((crc32 & 0x0000ff00) >> 8) & 0xFF), 
        (((crc32 & 0x000000ff) >> 0) & 0xFF));
    return true;
}

int makeLR2Date(int tid, const std::filesystem::path& filepath)
{
    // These code are from reverse-engineering. Please forgive me

    if (filepath.empty() && !std::filesystem::exists(filepath))
    {
        return 0;
    }

    static WIN32_FILE_ATTRIBUTE_DATA g_data[MAX_THREADS + 1];
    static FILETIME g_ft[MAX_THREADS + 1];
    static SYSTEMTIME g_st[MAX_THREADS + 1];

    auto& data = g_data[tid + 1];
    auto& ft = g_ft[tid + 1];
    auto& st = g_st[tid + 1];

    if (!filepath.empty())
    {
        if (0 == GetFileAttributesEx(filepath.native().c_str(), GetFileExInfoStandard, &data))
        {
            std::cout << w2cp(filepath.native()) << ": GetFileAttributesEx Error " << GetLastError() << std::endl;
            return -1;
        }
        ft = data.ftLastWriteTime;
    }
    else
    {
        GetSystemTime(&st);
        if (0 == SystemTimeToFileTime(&st, &ft))
        {
            std::cout << w2cp(filepath.native()) << ": SystemTimeToFileTime Error " << GetLastError() << std::endl;
            return -1;
        }
    }

    if (0 == FileTimeToSystemTime(&ft, &st))
    {
        std::cout << w2cp(filepath.native()) << ": FileTimeToSystemTime Error " << GetLastError() << std::endl;
        return -1;
    }

    // ???
    st.wYear += 0xFE8F;

    if (0 == SystemTimeToFileTime(&st, &ft))
    {
        // This call CAN FAIL according to asm. Who knows?
        //std::cout << w2cp(filepath.native()) << ": SystemTimeToFileTime Error " << GetLastError() << std::endl;
        //return -1;
    }

    // real magic happens below
    if (ft.dwHighDateTime < 0)
        ft.dwHighDateTime += 4.294967296e9;

    if (ft.dwLowDateTime < 0)
        ft.dwLowDateTime += 4.294967296e9;

    return (int)(ft.dwLowDateTime / 1.0e7) - (int)(ft.dwHighDateTime * -429.4836225);
}

bool md5Content(const std::string& content, char* output)
{
    BCRYPT_HASH_HANDLE      hHash = NULL;
    NTSTATUS                status = STATUS_UNSUCCESSFUL;
    PBYTE                   pbHash = NULL;
    bool ok = true;

    //allocate the hash buffer on the heap
    if (ok && NULL == (pbHash = (PBYTE)HeapAlloc(
        GetProcessHeap(), 0, cbHash)))
    {
        std::cout << "HeapAlloc pbHash failed" << std::endl;
        ok = false;
    }

    //create a hash
    if (ok && !NT_SUCCESS(status = BCryptCreateHash(
        hAlg,
        &hHash,
        NULL,
        0,
        NULL,
        0,
        0)))
    {
        std::cout << "BCryptCreateHash failed: " << status << std::endl;
        ok = false;
    }

    //hash some data
    if (ok && !NT_SUCCESS(status = BCryptHashData(
        hHash,
        (PBYTE)content.data(),
        content.length(),
        0)))
    {
        std::cout << "BCryptHashData failed: " << status << std::endl;
        ok = false;
    }

    //close the hash
    if (ok && !NT_SUCCESS(status = BCryptFinishHash(
        hHash,
        pbHash,
        cbHash,
        0)))
    {
        std::cout << "BCryptFinishHash failed: " << status << std::endl;
        ok = false;
    }

    if (ok)
    {
        for (DWORD i = 0; i < cbHash; i++)
        {
            sprintf_s(&output[i * 2], 3, "%02x", pbHash[i]);
        }
    }

    if (hHash)
    {
        BCryptDestroyHash(hHash);
    }

    if (pbHash)
    {
        HeapFree(GetProcessHeap(), 0, pbHash);
    }

    return ok;
}

std::vector<std::filesystem::path> dir(const std::filesystem::path& root)
{
    // Using FindFirstFileExW with FindExInfoBasic op is way faster 
    // than other implementations, namely std::filesystem::directory_iterator.
    // I was thinking about using system("dir"), but that is another hell
    // of encoding problems.

    std::vector<std::filesystem::path> result;

    WIN32_FIND_DATAW buf = { 0 };
    std::wstring rootAbs = std::filesystem::absolute(root).native();
    if (rootAbs.back() != L'\\')
        rootAbs = rootAbs + L"\\";
    rootAbs = rootAbs + L"*";
    HANDLE findHandle = FindFirstFileExW(
        rootAbs.c_str(),
        FindExInfoBasic,
        &buf,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_LARGE_FETCH);
    if (findHandle == INVALID_HANDLE_VALUE)
        return {};

    BOOL success = true;
    while (success)
    {
        if (lstrcmpW(buf.cFileName, L".") != 0 && lstrcmpW(buf.cFileName, L"..") != 0)
            result.push_back(std::filesystem::path(root) / buf.cFileName);
        success = FindNextFileW(findHandle, &buf);
    }

    FindClose(findHandle);
    return result;
}


std::string w2cp(const std::wstring& w, int cp)
{
    std::string u;
    int l = WideCharToMultiByte(cp, 0, w.c_str(), w.length(), NULL, 0, NULL, NULL);
    u.resize(l);
    l = WideCharToMultiByte(cp, 0, w.c_str(), w.length(), u.data(), l, NULL, NULL);
    return u;
}

std::wstring cp2w(const std::string& s, int cp)
{
    std::wstring w;
    int l = MultiByteToWideChar(cp, 0, s.c_str(), s.length(), NULL, 0);
    w.resize(l);
    l = MultiByteToWideChar(cp, 0, s.c_str(), s.length(), w.data(), l);
    return w;
}