
***

# DMG

## Apple Disk Image format

![https://github.com/seanpm2001/WacOS/blob/master/Graphics/DMG/Mac_OS_X_Disk_Image.png](https://github.com/seanpm2001/WacOS/blob/master/Graphics/DMG/Mac_OS_X_Disk_Image.png)

The Apple Disk image format (*.dmg, *smi, or *.img) is a proprietary disk image format commonly used with MacOS since Mac OS 9. It can be mounted by dragging it onto the dock, and then it can function as an application installer to install a program.

## Metadata

The file format is encoded in Big Endian. Despite the format being proprietary and poorly documented, it has since been successfully reverse engineered. It uses UDIF (Universal Disk Image Format) Metadata, which can be written in C like so:

```c
typedef struct {
  uint8_t  Signature[4];           // magic 'koly'
  uint32_t Version;                // 4 (as of 2013)
  uint32_t HeaderSize;             // sizeof(this) =  512 (as of 2013)
  uint32_t Flags;                 
  uint64_t RunningDataForkOffset;
  uint64_t DataForkOffset;         // usually 0, beginning of file
  uint64_t DataForkLength;
  uint64_t RsrcForkOffset;         // resource fork offset and length
  uint64_t RsrcForkLength;        
  uint32_t SegmentNumber;          // Usually 1, can be 0
  uint32_t SegmentCount;           // Usually 1, can be 0
  uuid_t   SegmentID; 
  uint32_t DataChecksumType;       // Data fork checksum
  uint32_t DataChecksumSize;
  uint32_t DataChecksum[32];
  uint64_t XMLOffset;              // Position of XML property list in file
  uint64_t XMLLength; 
  uint8_t  Reserved1[120];
  uint32_t ChecksumType;           // Master checksum
  uint32_t ChecksumSize;
  uint32_t Checksum[32];
  uint32_t ImageVariant;           // Unknown, commonly 1
  uint64_t SectorCount;
  uint32_t reserved2;
  uint32_t reserved3;
  uint32_t reserved4;
} __attribute__((packed, scalar_storage_order("big-endian"))) UDIFResourceFile;
```

The metadata goes at the end of the file, not the beginning.

***

## Sources

[Wikipedia](https://en.wikipedia.org/wiki/Apple_Disk_Image)

More sources needed, Wikipedia should not be the only source.

***

## Article info

**Written on:** `2021 Saturday September 18th at 9:39 pm`

**Last revised on:** `2021 Saturday September 18th at 9:39 pm`

**File format** `Markdown document (*.md *.mkd *.markdown)`

**Article version:** `1 (2021 Saturday September 18th at 9:39 pm)`

***
