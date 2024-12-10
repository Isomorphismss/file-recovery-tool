# FAT32 File Recovery Tool

This project is a robust utility for recovering deleted files from a FAT32 file system. It includes functionality to inspect file system details, list directory contents, and recover files (both contiguous and non-contiguous) with optional SHA-1 hash verification to ensure data integrity. The project leverages memory mapping (`mmap`) and handles FAT32-specific structures for efficient and reliable operations.

## Features
- **Print File System Information**: Display details about the FAT32 file system, including the number of FATs, bytes per sector, sectors per cluster, and reserved sectors.

- **List Root Directory**: List all valid entries in the root directory, including files and directories.

- **Recover Deleted Files**:
    - Recover contiguous files by restoring directory entries and FAT chains.
    - Recover non-contiguous files by traversing the FAT chain and verifying integrity using SHA-1.

- **SHA-1 Verification**: Ensure recovered file integrity by matching the computed SHA-1 hash with the provided value.

## Requirement
- POSIX-compliant system (e.g., Linux or macOS).
- C compiler (e.g., GCC).
- OpenSSL library for SHA-1 hash computations.

## Compilation
```bash
gcc -std=gnu17 -lcrypto -g fat32recover.c -o fat32recover -lm
```

## Usage
```bash
./fat32recover disk <options>
```

## Options
```bash
-i                     Print the file system information.
-l                     List the root directory.
-r filename [-s sha1]  Recover a contiguous file.
-R filename -s sha1    Recover a possibly non-contiguous file.
```

## Examples
- **Print File System Information**:
    ```bash
    ./fat32recover disk.img -i
    ```
    Output:
    ```bash
    Number of FATs = 2
    Bytes per sector = 512
    Sectors per cluster = 8
    Reserved sectors = 32
    ```

- **List Root Directory**:
    ```bash
    ./fat32recover disk.img -l
    ```
    Output:
    ```bash
    README.TXT (size = 12345, starting cluster = 5)
    FOLDER/ (starting cluster = 10)
    ```

- **Recover a Contiguous File**:
    ```bash
    ./fat32recover disk.img -r DELETED.TXT
    ```
    Output:
    ```bash
    DELETED.TXT: successfully recovered
    ```

- **Recover a Non-Contiguous File with SHA-1 Verification**:
    ```bash
    ./fat32recover disk.img -R DELETED.TXT -s 1a2b3c4d5e6f7g8h9i0j... (40-character SHA-1 hash)
    ```
    Output:
    ```bash
    DELETED.TXT: successfully recovered with SHA-1
    ```

## Implementation Details

### FAT32 Parsing

The tool reads FAT32-specific metadata structures, such as the Boot Sector and Directory Entries, to identify and manipulate file data. It maps the entire disk image into memory for efficient access using `mmap`.

### File Recovery

- **Contiguous Recovey**:
    - Modifies the directory entry to restore the deleted file's name.
    - Updates the FAT to rebuild the cluster chain.
- **Non-Contiguous Recovery**:
    - Traverses FAT chains to locate all clusters of a file.
    - Reconstructs the file data by stitching together the data from all clusters.
    - Optionally validates the reconstructed data using SHA-1.

### SHA-1 Verification

The tool uses OpenSSL's SHA-1 implementation to compute the hash of the reconstructed file. If the computed hash matches the user-provided SHA-1, the recovery is considered successful.

## Limitations

- Assumes the input disk image is a valid FAT32 file system.
- Non-contiguous file recovery requires the user to provide a SHA-1 hash.
- Files larger than the available memory may cause performance issues.
- The tool does not handle encrypted or compressed FAT32 volumes.

## File System Structures

### Boot Entry Structure
- `BS_jmpBoot`: Assembly instruction to jump to boot code.
- `BPB_BytsPerSec`: Bytes per sector (e.g., 512, 1024).
- `BPB_SecPerClus`: Sectors per cluster.
- `BPB_NumFATs`: Number of File Allocation Tables (FATs).
- `BPB_RsvdSecCnt`: Size in sectors of the reserved area.
- `BPB_RootClus`: Cluster where the root directory starts.

### Directory Entry Structure
- `DIR_Name`: File name (11-character padded string).
- `DIR_Attr`: File attributes (e.g., read-only, hidden).
- `DIR_FileSize`: File size in bytes.
- `DIR_FstClusLO` and `DIR_FstClusHI`: Starting cluster of the file.

## License
This project is licensed under the MIT License.
