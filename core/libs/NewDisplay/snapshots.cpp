#include "FT812.h"
#include "TrackerImpl.h"
#include "display.h"

static const uint8_t bytesPerPixel = 4;
static const uint32_t bmpWidth = 800;
static const uint32_t bmpHeight = 480;
static const uint32_t fileHeaderSize = 14;
static const uint32_t infoHeaderSize = 40; // 56
static const uint32_t bmpDataSize = bmpWidth * bmpHeight * bytesPerPixel;
static const uint32_t fileSize = fileHeaderSize + infoHeaderSize + bmpDataSize;

// clang-format off

static const unsigned char fileHeader[] = {
    'B',
    'M', /// signature
    (uint8_t)(fileSize),
    (uint8_t)(fileSize >> 8),
    (uint8_t)(fileSize >> 16),
    (uint8_t)(fileSize >> 24), /// image file size in bytes
    0, 0, 0, 0, /// reserved
    (uint8_t)(fileHeaderSize + infoHeaderSize),  0, 0, 0, /// start of pixel array
};

static const unsigned char infoHeader[] = {
    (uint8_t)(infoHeaderSize),
    0,
    0,
    0, /// header size
    (uint8_t)(bmpWidth),
    (uint8_t)(bmpWidth >> 8),
    (uint8_t)(bmpWidth >> 16),
    (uint8_t)(bmpWidth >> 24), /// image width
    (uint8_t)(bmpHeight),
    (uint8_t)(bmpHeight >> 8),
    (uint8_t)(bmpHeight >> 16),
    (uint8_t)(bmpHeight >> 24), /// image height
    1, 0, /// number of color planes
    bytesPerPixel * 8, 0, /// bits per pixel
    0, 0, 0, 0, /// compression //(3)
    0, 0, 0, 0, /// image size
	0x13, 0x0B, 0, 0, /// horizontal resolution
	0x13, 0x0B, 0, 0, /// vertical resolution
    0, 0, 0, 0, /// colors in color table
    0, 0, 0, 0, /// important colors
/*
    0, 0, 255, 0, /// r
    0, 255, 0, 0, /// g
    255, 0, 0, 0, /// b
    0, 0, 0, 255, /// a
*/
};

// clang-format on

extern uint8_t lcdPclk;
extern uint8_t lcdRotate;
void cDisplay::doScreenShot()
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();

    if (!fileSystem->exists("Snapshots"))
        fileSystem->mkdir(0, "Snapshots");

    char bmpName[32];
    uint16_t num = 1;
    sprintf(bmpName, "Snapshots/Snapshot_%03d.bmp", 1);
    while (fileSystem->exists(bmpName))
    {
        num++;
        sprintf(bmpName, "Snapshots/Snapshot_%03d.bmp", num);
    }

    auto [isOpen, bmpHandle] =
      fileSystem->openFile(bmpName, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
    {
        return;
    }
    fileSystem->writeFile(fileHeader, fileHeaderSize, bmpHandle);
    fileSystem->writeFile(infoHeader, infoHeaderSize, bmpHandle);

    // Capture in strips of 60 rows (800 x 60 x 4 = 192 kB) into the controls
    // area of RAM_G. The previous code asked the chip for a 1600 x 240 snapshot
    // (1.5 MB) from an address 150000 bytes into a 1 MiB RAM, which overran
    // graphics memory and could stall the co-processor. BMP rows are stored
    // bottom-up, so walk the strips from the bottom of the screen.
    constexpr int kStripRows = 60;
    constexpr uint32_t kRowBytes = bmpWidth * bytesPerPixel;
    uint8_t buf[kRowBytes];

    for (int strip = (int)(bmpHeight / kStripRows) - 1; strip >= 0; strip--)
    {
        displayDriver->beginCoProList();
        API_CMD_SNAPSHOT2(0x20, controlsRamStartAddress, 0, strip * kStripRows, bmpWidth, kStripRows);
        displayDriver->endCoProList();
        displayDriver->awaitCoProEmpty();

        for (int row = kStripRows - 1; row >= 0; row--)
        {
            displayDriver->readDataRAMG(buf, kRowBytes, controlsRamStartAddress + row * kRowBytes);
            fileSystem->writeFile(buf, kRowBytes, bmpHandle);
        }
    }

    fileSystem->closeFile(bmpHandle);

    // The strips overwrote the cached control display lists; clear and redraw.
    displayDriver->beginCoProList();
    API_CMD_MEMZERO(controlsRamStartAddress, kStripRows * kRowBytes);
    displayDriver->endCoProList();

    resetControlQueue();
    for (uint8_t i = 0; i < controlsCount; i++)
    {
        // Only redraw what is on screen. Hidden controls may hold stale data
        // pointers from a previous module; updating them read invalid memory
        // (found as a crash in the Linux port, a possible hang on the device).
        cDisplayControl* p = controlsTable[i];
        if (p != nullptr && (p->style & controlStyleShow))
        {
            refreshControl(i);
        }
    }
}

void cDisplay::scheduleScreenShot(int timeSec)
{
    screenShotscheduled = true;
    screenShotTimer = timer->millis() + timeSec * 1000;
}
