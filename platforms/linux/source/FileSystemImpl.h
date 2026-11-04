#ifndef FILE_SYSTEM_IMPL_H_
#define FILE_SYSTEM_IMPL_H_
#include "Platform.h"
#include <string>
#include <vector>

// POSIX implementation of the core's FileSystem interface over a directory
// that mirrors the SD card. Semantics follow platforms/mk66 (FatFs) closely:
//  - hidden = name starts with '.' or is "System Volume Information"
//  - directory listings are returned sorted case-insensitively (FatFs
//    returns physical order, which a host copy cannot reproduce)
//  - "fast seek" + raw sector reads are emulated with a fake, fully
//    contiguous cluster chain per open file (see enableFastSeek)
class FileSystemImpl final : public FileSystem
{
  public:
    static constexpr int kPoolSize = 64;
    static constexpr uint32_t kSectorSize = 512;
    static constexpr uint32_t kSectorsPerCluster = 8;
    static constexpr uint32_t kDatabaseSector = 8192;
    static constexpr uint32_t kClustersPerSlot = 1u << 20; // 4 GiB of file per slot

    void init(const std::string& rootDir);
    const std::string& root() const { return rootDir; }

    bool isSdCardInserted() override;
    void setMountEventCallback(CardStateChangeCallback funct) override;
    bool exists(const char* path) override;
    bool rename(const char* oldPath, const char* newPath) override;
    bool mkdir(uint8_t hidden, const char* path, bool pFlag = true) override;
    bool remove(const char* path) override;
    bool removeDirWithFiles(const char* path) override;
    bool removeOnlyDirFiles(const char* path) override;
    size_t getFileSize(const char* path) override;
    size_t getFileSize(FileHandle fileHandle) override;
    size_t getFileAvailable(FileHandle fileHandle) override;
    std::tuple<bool, FileHandle> openFile(const char* filepath,
                                          FileOpenMode openMode = kRead,
                                          FileHandle* fileHandle = nullptr) override;
    size_t readFile(void* destBuf, size_t bufLen, FileHandle file) override;
    size_t writeFile(const void* data, size_t dataSize, FileHandle fileHandle) override;
    bool seekFileCur(int offset, FileHandle fileHandle) override;
    bool seekFile(uint32_t pos, FileHandle fileHandle) override;
    void closeFile(FileHandle file, bool deleteOnClose = true) override;
    void endFile(FileHandle file) override;
    bool isFileOpen(FileHandle file) override;
    bool enableFastSeek(FileHandle fileHandle,
                        uint32_t* clmtBuffer,
                        uint32_t clmtElementCount,
                        uint32_t* outDatabaseSector = nullptr,
                        uint32_t* outSectorsPerCluster = nullptr) override;
    bool readSectorsAsync(uint8_t* buffer, uint32_t startSector, uint32_t sectorCount) override;
    bool pollAsyncRead(bool* outSuccess = nullptr) override;
    bool waitAsyncRead() override;
    bool isAsyncReadBusy() override;
    int createInstrumentsList(const char* path,
                              char** list,
                              uint8_t list_length,
                              uint32_t* sizeOut) override;
    int countContainedFiles(const char* path) override;
    int createFilesList(const char* path,
                        uint8_t start_line,
                        char** list,
                        uint8_t list_length,
                        uint16_t max_used_memory,
                        enListFileFilter filter = enListFileFilter_all,
                        uint16_t skip_count = 0,
                        uint16_t max_files_in_subdir = 0,
                        enListFileFilter folderSizeFilter = enListFileFilter_all) override;
    void listDirContents(const char* dirpath,
                         DirContentType contentType,
                         std::function<bool(const char* name)> nameFilter,
                         char* namesBuffer,
                         int namesBufferSize,
                         int maxNameLen,
                         int& outNamesCount,
                         char** fileList) override;
    int readNbFiles(const char* romPath) override;
    bool nesDir_open(const char* path) override;
    bool nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override;
    void nesDir_close() override;

    struct Entry
    {
        std::string name;
        bool isDir = false;
        bool hidden = false;
        uint32_t size = 0;
    };
    bool readDir(const std::string& devicePath, std::vector<Entry>& out);

  private:
    struct File
    {
        bool used = false;
        bool open = false;
        int fd = -1;
        std::string path;
    };
    File pool[kPoolSize];
    File* allocFile();
    void freeFile(File* f);
    int slotOf(const File* f) const { return (int)(f - pool); }

    std::string hostPath(const char* devicePath) const;
    static bool isMacMetaFile(const std::string& name);
    static bool isMacMetaFolder(const std::string& name);
    static bool isHiddenName(const std::string& name);
    bool filterFileEntry(const Entry& e, const std::string& dirPath, enListFileFilter filter);
    uint16_t countFilesInSubDirectory(const std::string& dirPath,
                                      const std::string& dirName,
                                      enListFileFilter filter);

    std::string rootDir;
    CardStateChangeCallback mountCallback = nullptr;
    bool asyncDone = false;
    bool asyncOk = false;
    std::vector<Entry> nesEntries;
    size_t nesPos = 0;
    bool nesOpen = false;
};

#endif
