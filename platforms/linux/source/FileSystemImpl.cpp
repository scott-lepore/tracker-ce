#include "FileSystemImpl.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// ------------------------------------------------------------ helpers -----
void FileSystemImpl::init(const std::string& dir)
{
    rootDir = dir;
    while (rootDir.size() > 1 && rootDir.back() == '/')
        rootDir.pop_back();
}

std::string FileSystemImpl::hostPath(const char* devicePath) const
{
    std::string p = devicePath ? devicePath : "";
    while (!p.empty() && p[0] == '/')
        p.erase(0, 1);
    if (p.empty())
        return rootDir;
    return rootDir + "/" + p;
}

bool FileSystemImpl::isMacMetaFile(const std::string& n)
{
    return n.size() >= 2 && n[0] == '.' && n[1] == '_';
}

bool FileSystemImpl::isMacMetaFolder(const std::string& n)
{
    return n == "__MACOSX";
}

bool FileSystemImpl::isHiddenName(const std::string& n)
{
    // FAT hidden attribute does not survive a copy to ext4; mirror what the
    // firmware hides on a real card: its own Workspace (created hidden) and the
    // legacy root Instruments folder.
    return (!n.empty() && n[0] == '.') || n == "System Volume Information" || n == "Workspace" ||
           n == "Instruments";
}

bool FileSystemImpl::readDir(const std::string& devicePath, std::vector<Entry>& out)
{
    out.clear();
    std::string hp = hostPath(devicePath.c_str());
    DIR* d = opendir(hp.c_str());
    if (!d)
        return false;
    while (dirent* de = readdir(d))
    {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        Entry e;
        e.name = de->d_name;
        struct stat st;
        if (stat((hp + "/" + e.name).c_str(), &st) != 0)
            continue;
        e.isDir = S_ISDIR(st.st_mode);
        e.size = e.isDir ? 0 : (uint32_t)st.st_size;
        e.hidden = isHiddenName(e.name);
        out.push_back(e);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        int c = strcasecmp(a.name.c_str(), b.name.c_str());
        return c != 0 ? c < 0 : a.name < b.name;
    });
    return true;
}

FileSystemImpl::File* FileSystemImpl::allocFile()
{
    for (auto& f : pool)
    {
        if (!f.used)
        {
            f = File{};
            f.used = true;
            return &f;
        }
    }
    return nullptr;
}

void FileSystemImpl::freeFile(File* f)
{
    if (f)
        *f = File{};
}

// -------------------------------------------------------------- card -----
bool FileSystemImpl::isSdCardInserted()
{
    struct stat st;
    return stat(rootDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void FileSystemImpl::setMountEventCallback(CardStateChangeCallback funct)
{
    mountCallback = funct;
}

// ------------------------------------------------------------- paths -----
bool FileSystemImpl::exists(const char* path)
{
    struct stat st;
    return stat(hostPath(path).c_str(), &st) == 0;
}

bool FileSystemImpl::rename(const char* oldPath, const char* newPath)
{
    return ::rename(hostPath(oldPath).c_str(), hostPath(newPath).c_str()) == 0;
}

bool FileSystemImpl::mkdir(uint8_t, const char* path, bool)
{
    std::string hp = hostPath(path);
    if (::mkdir(hp.c_str(), 0775) == 0)
        return true;
    return errno == EEXIST; // FatFs: FR_EXIST is reported but treated as success
}

bool FileSystemImpl::remove(const char* path)
{
    return ::remove(hostPath(path).c_str()) == 0;
}

namespace
{
int rmAll(const char* fpath, const struct stat*, int typeflag, struct FTW*)
{
    return (typeflag == FTW_DP || typeflag == FTW_D) ? ::rmdir(fpath) : ::unlink(fpath);
}
int rmFilesOnly(const char* fpath, const struct stat*, int typeflag, struct FTW*)
{
    if (typeflag == FTW_DP || typeflag == FTW_D)
        return 0;
    return ::unlink(fpath);
}
} // namespace

bool FileSystemImpl::removeDirWithFiles(const char* path)
{
    return nftw(hostPath(path).c_str(), rmAll, 16, FTW_DEPTH | FTW_PHYS) == 0;
}

bool FileSystemImpl::removeOnlyDirFiles(const char* path)
{
    return nftw(hostPath(path).c_str(), rmFilesOnly, 16, FTW_DEPTH | FTW_PHYS) == 0;
}

// ------------------------------------------------------------- files -----
size_t FileSystemImpl::getFileSize(const char* path)
{
    struct stat st;
    if (stat(hostPath(path).c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return 0;
    return (size_t)st.st_size;
}

size_t FileSystemImpl::getFileSize(FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return 0;
    struct stat st;
    if (fstat(f->fd, &st) != 0)
        return 0;
    return (size_t)st.st_size;
}

size_t FileSystemImpl::getFileAvailable(FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return 0;
    off_t pos = lseek(f->fd, 0, SEEK_CUR);
    size_t size = getFileSize(fileHandle);
    if (pos < 0 || (size_t)pos >= size)
        return 0;
    return size - (size_t)pos;
}

std::tuple<bool, FileSystem::FileHandle> FileSystemImpl::openFile(const char* filepath,
                                                                  FileOpenMode openMode,
                                                                  FileHandle* fileHandle)
{
    File* f;
    if (fileHandle == nullptr)
    {
        f = allocFile();
    }
    else if (*fileHandle == nullptr)
    {
        f = allocFile();
        *fileHandle = (FileHandle)f;
    }
    else
    {
        f = (File*)*fileHandle;
    }
    if (f == nullptr)
        return std::make_tuple(false, nullptr);

    if (f->open)
    {
        ::close(f->fd);
        f->fd = -1;
        f->open = false;
    }

    int flags = O_RDONLY;
    bool seekEnd = false;
    switch (openMode)
    {
        case kWriteOpen: flags = O_RDWR | O_CREAT; break;                // FA_OPEN_ALWAYS
        case kWriteAppend: flags = O_RDWR | O_CREAT; seekEnd = true; break; // FA_OPEN_APPEND
        case kWriteCreateNew: flags = O_RDWR | O_CREAT | O_TRUNC; break; // FA_CREATE_ALWAYS
        default: break;
    }
    int fd = ::open(hostPath(filepath).c_str(), flags | O_CLOEXEC, 0664);
    bool isOpen = fd >= 0;
    if (isOpen)
    {
        f->fd = fd;
        f->open = true;
        f->path = filepath;
        if (seekEnd)
            lseek(fd, 0, SEEK_END);
    }
    else if (fileHandle == nullptr)
    {
        freeFile(f);
        f = nullptr;
    }
    return std::make_tuple(isOpen, (FileHandle)f);
}

int32_t FileSystemImpl::readFile(void* destBuf, size_t bufLen, FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return -1;
    ssize_t n = ::read(f->fd, destBuf, bufLen);
    return n < 0 ? -1 : (int32_t)n; // like SdFat: -1 on error
}

int32_t FileSystemImpl::writeFile(const void* data, size_t dataSize, FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return -1;
    ssize_t n = ::write(f->fd, data, dataSize);
    return n < 0 ? -1 : (int32_t)n;
}

bool FileSystemImpl::seekFileCur(int offset, FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return false;
    if (offset == 0)
        return true;
    return lseek(f->fd, offset, SEEK_CUR) >= 0;
}

bool FileSystemImpl::seekFile(uint32_t pos, FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return false;
    // f_lseek beyond EOF on a writable file extends it; emulate for writes only
    return lseek(f->fd, (off_t)pos, SEEK_SET) >= 0;
}

void FileSystemImpl::closeFile(FileHandle fileHandle, bool deleteOnClose)
{
    File* f = (File*)fileHandle;
    if (!f)
        return;
    if (f->open)
    {
        ::close(f->fd);
        f->fd = -1;
        f->open = false;
    }
    if (deleteOnClose)
        freeFile(f);
}

void FileSystemImpl::endFile(FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open)
        return;
    off_t pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos >= 0)
        (void)!ftruncate(f->fd, pos);
}

bool FileSystemImpl::isFileOpen(FileHandle fileHandle)
{
    File* f = (File*)fileHandle;
    return f && f->open;
}

// --------------------------------------------------- streaming sectors ----
// Each pool slot owns a private range of fake clusters; a file is presented
// as one contiguous run so translateOffsetToLba() in the core yields
// LBA = database + (slotBase + offset/clusterBytes - 2) * csize + csect.
bool FileSystemImpl::enableFastSeek(FileHandle fileHandle,
                                    uint32_t* clmtBuffer,
                                    uint32_t clmtElementCount,
                                    uint32_t* outDatabaseSector,
                                    uint32_t* outSectorsPerCluster)
{
    File* f = (File*)fileHandle;
    if (!f || !f->open || clmtBuffer == nullptr || clmtElementCount < 4)
        return false;
    const uint32_t clusterBytes = kSectorSize * kSectorsPerCluster;
    uint32_t size = (uint32_t)getFileSize(fileHandle);
    uint32_t clusters = (size + clusterBytes - 1) / clusterBytes;
    if (clusters == 0)
        clusters = 1;
    clmtBuffer[0] = clmtElementCount;
    clmtBuffer[1] = clusters;
    clmtBuffer[2] = 2 + (uint32_t)(slotOf(f) + 1) * kClustersPerSlot;
    clmtBuffer[3] = 0;
    if (outDatabaseSector)
        *outDatabaseSector = kDatabaseSector;
    if (outSectorsPerCluster)
        *outSectorsPerCluster = kSectorsPerCluster;
    return true;
}

bool FileSystemImpl::readSectorsAsync(uint8_t* buffer, uint32_t startSector, uint32_t sectorCount)
{
    asyncDone = true;
    asyncOk = false;
    if (startSector < kDatabaseSector)
        return false;
    uint32_t rel = startSector - kDatabaseSector;
    uint32_t cluster = rel / kSectorsPerCluster + 2;
    uint32_t csect = rel % kSectorsPerCluster;
    int slot = (int)(cluster / kClustersPerSlot) - 1;
    if (slot < 0 || slot >= kPoolSize)
        return false;
    File* f = &pool[slot];
    if (!f->open)
        return false;
    uint32_t base = 2 + (uint32_t)(slot + 1) * kClustersPerSlot;
    uint64_t offset = ((uint64_t)(cluster - base) * kSectorsPerCluster + csect) * kSectorSize;
    size_t want = (size_t)sectorCount * kSectorSize;
    size_t got = 0;
    while (got < want)
    {
        ssize_t n = pread(f->fd, buffer + got, want - got, (off_t)(offset + got));
        if (n <= 0)
            break;
        got += (size_t)n;
    }
    if (got < want)
        memset(buffer + got, 0, want - got); // past EOF: zero like unused sectors
    asyncOk = true;
    return true;
}

bool FileSystemImpl::pollAsyncRead(bool* outSuccess)
{
    if (outSuccess)
        *outSuccess = asyncOk;
    return asyncDone;
}

bool FileSystemImpl::waitAsyncRead()
{
    return asyncOk;
}

bool FileSystemImpl::isAsyncReadBusy()
{
    return false;
}

// ----------------------------------------------------------- listings -----
namespace
{
bool hasExt(const std::string& n, const char* ext3) // ".wav"-style, case-insensitive
{
    size_t len = n.size();
    if (len < 5)
        return false;
    return n[len - 4] == '.' && tolower((unsigned char)n[len - 3]) == ext3[0] &&
           tolower((unsigned char)n[len - 2]) == ext3[1] &&
           tolower((unsigned char)n[len - 1]) == ext3[2];
}
std::string lower(const std::string& s)
{
    std::string r = s;
    for (auto& c : r)
        c = (char)tolower((unsigned char)c);
    return r;
}
} // namespace

bool FileSystemImpl::filterFileEntry(const Entry& e, const std::string& dirPath, enListFileFilter filter)
{
    if (e.hidden)
        return false;
    if (e.isDir && filter != enListFileFilter_folder && filter != enListFileFilter_all)
        return false;
    size_t filenameLen = e.name.size();
    switch (filter)
    {
        case enListFileFilter_all: return true;
        case enListFileFilter_folder: return e.isDir && !isMacMetaFolder(e.name);
        case enListFileFilter_wave_inst:
        case enListFileFilter_wave:
        {
            if (filenameLen < 5)
                return false;
            if (!hasExt(e.name, "wav"))
            {
                if (!(filter == enListFileFilter_wave_inst && hasExt(e.name, "pti")))
                    return false;
            }
            if (dirPath.size() + filenameLen > 253)
                return false;
            return !isMacMetaFile(e.name);
        }
        case enListFileFilter_firmware:
        {
            if (filenameLen < 15)
                return false;
            if (hasExt(e.name, "ptf"))
            {
                if (e.name.compare(0, 11, "PolyendTrac") != 0)
                    return false;
            }
            else if (hasExt(e.name, "hex"))
            {
                if (e.name.compare(0, 2, "mt") != 0)
                    return false;
            }
            else
                return false;
            return !isMacMetaFile(e.name);
        }
        case enListFileFilter_mod:
        {
            std::string l = lower(e.name);
            return l.find(".mod") != std::string::npos || l.find(".it") != std::string::npos;
        }
        case enListFileFilter_game:
            return !isMacMetaFile(e.name) && lower(e.name).find(".nes") != std::string::npos;
    }
    return false;
}

uint16_t FileSystemImpl::countFilesInSubDirectory(const std::string& dirPath,
                                                  const std::string& dirName,
                                                  enListFileFilter filter)
{
    std::vector<Entry> entries;
    std::string sub = dirPath + "/" + dirName;
    if (!readDir(sub, entries))
        return 0;
    uint16_t count = 0;
    for (const auto& e : entries)
        if (filterFileEntry(e, sub, filter))
            count++;
    return count;
}

int FileSystemImpl::createFilesList(const char* path,
                                    uint8_t start_line,
                                    char** list,
                                    uint8_t list_length,
                                    uint16_t max_used_memory,
                                    enListFileFilter filter,
                                    uint16_t skip_count,
                                    uint16_t max_files_in_subdir,
                                    enListFileFilter folderSizeFilter)
{
    std::vector<Entry> entries;
    if (!readDir(path, entries))
        return kListError;

    for (uint8_t i = 0; i < list_length; i++)
    {
        if (list[i] != nullptr)
        {
            delete[] list[i];
            list[i] = nullptr;
        }
    }
    uint8_t n = start_line;
    if (max_used_memory < 255)
        return n;
    uint16_t memory_used = 0;
    std::string dirPath = path;

    for (const auto& e : entries)
    {
        if (n >= list_length)
            break;
        if (memory_used > max_used_memory - 255)
            break;
        if (!filterFileEntry(e, dirPath, filter))
            continue;
        if (skip_count > 0)
        {
            skip_count--;
            continue;
        }
        uint16_t len = (uint16_t)e.name.size() + (e.isDir ? 2 : 1);
        list[n] = new char[len];
        memory_used += len;
        if (e.isDir)
        {
            strcpy(list[n], "/");
            strcat(list[n], e.name.c_str());
            int16_t files_count = (int16_t)countFilesInSubDirectory(dirPath, e.name, folderSizeFilter);
            if (max_files_in_subdir != 0 && files_count > max_files_in_subdir)
            {
                uint16_t offset = 0;
                len += 4 + 2 * 5;
                while (files_count > 0 && n < list_length - 1)
                {
                    if (list[n] != nullptr)
                    {
                        delete[] list[n];
                        memory_used -= len;
                    }
                    list[n] = new char[len];
                    memory_used += len;
                    snprintf(list[n], len, "/%s (%d-%d)", e.name.c_str(), offset + 1,
                             offset + max_files_in_subdir);
                    files_count -= max_files_in_subdir;
                    n++;
                    offset += max_files_in_subdir;
                }
                n--;
            }
        }
        else
        {
            strcpy(list[n], e.name.c_str());
        }
        n++;
    }
    return n;
}

int FileSystemImpl::createInstrumentsList(const char* path,
                                          char** list,
                                          uint8_t list_length,
                                          uint32_t* sizeOut)
{
    std::vector<Entry> entries;
    if (!readDir(path, entries))
        return kListError;
    uint8_t n = 0;
    for (const auto& e : entries)
    {
        if (n >= list_length)
            break;
        if (e.hidden || e.isDir)
            continue;
        if (!hasExt(e.name, "pti"))
            continue;
        const char* p = e.name.c_str();
        int idx1 = -1, idx2 = -1;
        if (*p >= '0' && *p <= '9')
            idx1 = *p - '0';
        else
            continue;
        p++;
        if (*p >= '0' && *p <= '9')
        {
            idx2 = *p - '0';
            p++;
        }
        if (*p != ' ')
            continue;
        p++;
        if (idx2 >= 0)
            idx1 = idx1 * 10 + idx2;
        if (idx1 < 1 || idx1 > 48)
            continue;
        idx1--;
        if (list[idx1] != nullptr)
            continue;
        int len = (int)e.name.size() - (int)(p - e.name.c_str()) - 4;
        list[idx1] = new char[len + 1];
        strncpy(list[idx1], p, len);
        list[idx1][len] = 0;
        *sizeOut += e.size;
        n++;
    }
    return n;
}

int FileSystemImpl::countContainedFiles(const char* path)
{
    std::vector<Entry> entries;
    if (!readDir(path, entries))
        return 0;
    std::string dirPath = path;
    int n = 0;
    for (const auto& e : entries)
    {
        if (e.hidden)
            continue;
        if (!e.isDir)
        {
            if (!hasExt(e.name, "wav"))
                continue;
            if (dirPath.size() + e.name.size() > 253)
                continue;
        }
        n++;
    }
    return n;
}

extern bool compareStrings(const char* a, const char* b);

void FileSystemImpl::listDirContents(const char* dirPath,
                                     DirContentType contentType,
                                     std::function<bool(const char* name)> nameFilter,
                                     char* namesBuffer,
                                     int namesBufferSize,
                                     int maxNameLen,
                                     int& count,
                                     char** fileList)
{
    std::vector<Entry> entries;
    if (!readDir(dirPath, entries))
    {
        count = 0;
        return;
    }
    int listPos = 0;
    for (const auto& e : entries)
    {
        if (listPos >= count)
            break;
        if (e.hidden)
            continue;
        int nameLen;
        if (contentType == kFolders)
        {
            if (!e.isDir || isMacMetaFolder(e.name))
                continue;
            if (nameFilter && !nameFilter(e.name.c_str()))
                continue;
            nameLen = (int)e.name.size() + 2;
        }
        else if (contentType == kFiles)
        {
            if (e.isDir || isMacMetaFile(e.name))
                continue;
            if (nameFilter && !nameFilter(e.name.c_str()))
                continue;
            nameLen = (int)e.name.size() + 1;
        }
        else
            continue;
        if (nameLen > namesBufferSize)
            break;
        if (nameLen > maxNameLen)
            continue;
        fileList[listPos] = namesBuffer;
        if (e.isDir)
        {
            *namesBuffer = '/';
            namesBuffer++;
        }
        strncpy(namesBuffer, e.name.c_str(), maxNameLen);
        namesBuffer[maxNameLen] = 0;
        namesBuffer += nameLen;
        namesBufferSize -= nameLen;
        listPos++;
    }
    count = listPos;
    std::sort(fileList, fileList + count,
              [](const char* a, const char* b) { return compareStrings(a, b); });
}

int FileSystemImpl::readNbFiles(const char* romPath)
{
    std::vector<Entry> entries;
    if (!readDir(romPath, entries))
        return 0;
    return (int)entries.size();
}

bool FileSystemImpl::nesDir_open(const char* path)
{
    nesOpen = readDir(path, nesEntries);
    nesPos = 0;
    return nesOpen;
}

bool FileSystemImpl::nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size)
{
    if (!nesOpen || nesPos >= nesEntries.size())
        return false;
    const Entry& e = nesEntries[nesPos++];
    *isDir = e.isDir ? 1 : 0;
    *isHid = e.hidden ? 1 : 0;
    if (size)
        *size = e.size;
    strcpy(path, e.name.c_str());
    return true;
}

void FileSystemImpl::nesDir_close()
{
    nesOpen = false;
    nesEntries.clear();
    nesPos = 0;
}
