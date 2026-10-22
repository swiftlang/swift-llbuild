//===- FileInfo.h -----------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file contains the FileInfo wrapper which is shared by the Ninja and
// BuildSystem libraries.
//
// FIXME: I am ambivalent about this living in Basic, I want all non-functional
// pieces to generally be pretty isolated (and ideally always mediated by a
// delegate access). We may eventually want a specific FileSystem component for
// dealing with efficient and abstracted access to the file system and
// containing other pieces (like stat caching, or dealing with distribution or
// virtualization).
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_FILEINFO_H
#define LLBUILD_BASIC_FILEINFO_H

#include "BinaryCoding.h"

#include <cstdint>
#include <string>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include "llvm/Support/MD5.h"
#endif

namespace llbuild {
namespace basic {

/// File timestamp wrapper.
struct FileTimestamp {
  uint64_t seconds;
  uint64_t nanoseconds;
  
  bool operator==(const FileTimestamp& rhs) const {
    return seconds == rhs.seconds && nanoseconds == rhs.nanoseconds;
  }
  bool operator!=(const FileTimestamp& rhs) const {
    return !(*this == rhs);
  }
  bool operator<(const FileTimestamp& rhs) const {
    return (seconds < rhs.seconds ||
            (seconds == rhs.seconds && nanoseconds < rhs.nanoseconds));
  }
  bool operator<=(const FileTimestamp& rhs) const {
    return (seconds < rhs.seconds ||
            (seconds == rhs.seconds && nanoseconds <= rhs.nanoseconds));
  }
  bool operator>(const FileTimestamp& rhs) const {
    return rhs < *this;
  }
  bool operator>=(const FileTimestamp& rhs) const {
    return rhs <= *this;
  }
};

struct FileChecksum {
  uint8_t bytes[32] = {0};

  bool operator==(const FileChecksum& rhs) const {
    return (memcmp(bytes, rhs.bytes, sizeof(bytes)) == 0);
  }

  bool operator!=(const FileChecksum& rhs) const {
    return !(*this==rhs);
  }

  static FileChecksum getChecksumForPath(const std::string& path);
};

/// File information which is intended to be used as a proxy for when a file has
/// changed.
///
/// This structure is intentionally sized to have no packing holes.
struct FileInfo {
  /// The device number.
  uint64_t device;
  /// The inode number.
  uint64_t inode;
  /// The mode flags of the file.
  uint64_t mode;
  /// The size of the file.
  uint64_t size;
  /// The modification time of the file.
  FileTimestamp modTime;
  /// The checksum of the file.
  FileChecksum checksum = {};

  /// Check if this is a FileInfo representing a missing file.
  bool isMissing() const {
    // We use an all-zero FileInfo as a sentinel, under the assumption this can
    // never exist in normal circumstances.
    return (device == 0 && inode == 0 && mode == 0 && size == 0 &&
            modTime.seconds == 0 && modTime.nanoseconds == 0);
  }

  /// Check if the FileInfo corresponds to a directory.
  bool isDirectory() const;

  bool operator==(const FileInfo& rhs) const {
    return (device == rhs.device &&
            inode == rhs.inode &&
            size == rhs.size &&
            modTime == rhs.modTime &&
            checksum == rhs.checksum);
  }

  bool operator!=(const FileInfo& rhs) const {
    return !(*this == rhs);
  }

  /// Get the information to represent the state of the given node in the file
  /// system.
  ///
  /// \param asLink If yes, checks the information for the file path without
  /// looking through symbolic links.
  ///
  /// \returns The FileInfo for the given path, which will be missing if the
  /// path does not exist (or any error was encountered).
  static FileInfo getInfoForPath(const std::string& path, bool asLink = false);
};

template<>
struct BinaryCodingTraits<FileTimestamp> {
  static inline void encode(const FileTimestamp& value, BinaryEncoder& coder) {
    coder.write(value.seconds);
    coder.write(value.nanoseconds);
  }
  static inline void decode(FileTimestamp& value, BinaryDecoder& coder) {
    coder.read(value.seconds);
    coder.read(value.nanoseconds);
  }
};

template<>
struct BinaryCodingTraits<FileChecksum> {
  static inline void encode(const FileChecksum& value, BinaryEncoder& coder) {
    for(int i=0; i<32; i++) {
      coder.write(value.bytes[i]);
    }
  }
  static inline void decode(FileChecksum& value, BinaryDecoder& coder) {
    for(int i=0; i<32; i++) {
      coder.read(value.bytes[i]);
    }
  }
};

template<>
struct BinaryCodingTraits<FileInfo> {
  static inline void encode(const FileInfo& value, BinaryEncoder& coder) {
    coder.write(value.device);
    coder.write(value.inode);
    coder.write(value.mode);
    coder.write(value.size);
    coder.write(value.modTime);
    coder.write(value.checksum);
  }
  static inline void decode(FileInfo& value, BinaryDecoder& coder) {
    coder.read(value.device);
    coder.read(value.inode);
    coder.read(value.mode);
    coder.read(value.size);
    coder.read(value.modTime);
    coder.read(value.checksum);
  }
};

class FileChecksumHasher {
public:
  FileChecksumHasher(const std::string& path): path(path) { }

  virtual void copy(uint8_t *outputBuffer) = 0;

  bool readAndDigest() {
    this->file = std::fopen(path.c_str(), "rb");

    uint8_t buffer[4*4096];
    size_t bytesRead = 0;

    if (file != NULL) {
      while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        update(buffer, bytesRead);
      }
      fclose(file);
      finalize();
      return true;
    } else {
      return false;
    }
  }

  virtual void readPathStringAndDigest(FileChecksum& result) {
    update(reinterpret_cast<const uint8_t*>(&path[0]), path.size());
    finalize();
    copy(result.bytes);
  }

  virtual void finalize() = 0;

  virtual void update(const uint8_t *buffer, size_t bytesRead) = 0;

  virtual ~FileChecksumHasher() = default;

private:
  FILE *file = NULL;
  const std::string& path;
};

#ifndef __APPLE__
class FileChecksumHasherMD5: public FileChecksumHasher {
public:
  FileChecksumHasherMD5(const std::string& path): FileChecksumHasher(path) {
  }

  void copy(uint8_t *outputBuffer) override {
    std::copy(output.Bytes.begin(), output.Bytes.end(), outputBuffer);
  }

  void finalize() override {
    llvm::MD5::MD5Result output;
    hasher.final(output);
  }

  void update(const uint8_t *buffer, size_t bytesRead) override {
    const char* buffer2 = (char*) buffer;
    hasher.update(StringRef(buffer2, bytesRead));
  }

private:
  llvm::MD5 hasher;
  llvm::MD5::MD5Result output;
};

typedef FileChecksumHasherMD5 PlatformSpecificHasher;

#else

class FileChecksumHasherSHA256: public FileChecksumHasher {
public:
  FileChecksumHasherSHA256(const std::string& path): FileChecksumHasher(path) {
    CC_SHA256_Init(&ctx);
  }

  void copy(uint8_t *outputBuffer) override {
    std::copy(temp_digest, temp_digest+CC_SHA256_DIGEST_LENGTH, outputBuffer);
  }

  void finalize() override {
    CC_SHA256_Final(temp_digest, &ctx);
  }

  void update(const uint8_t *buffer, size_t bytesRead) override {
    CC_SHA256_Update(&ctx, buffer, bytesRead);
  }

private:
  CC_SHA256_CTX ctx = {};
  unsigned char temp_digest[CC_SHA256_DIGEST_LENGTH] = { 42 };
};

typedef FileChecksumHasherSHA256 PlatformSpecificHasher;
#endif

}
}

#endif
