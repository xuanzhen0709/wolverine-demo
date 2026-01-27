#pragma once

#include <cstddef>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <span>
#include <sys/mman.h>
#include <sys/stat.h>
#include <wolverine/logging.hpp>
#include "zpp_bits.h"

template <typename obj_type>
void serialize(const std::filesystem::path &path, const obj_type &obj)
{
  try {
    wllog_info("serializing to {}\n", path.native());
    std::vector<std::byte> buf;
    zpp::bits::out out(buf);
    out(obj).or_throw();
    std::ofstream ofs(path.native(), std::ios::binary);
    if (!ofs) {
      wllog_fatal("failed to open {}\n", path.native());
    }
    ofs.write(reinterpret_cast<const char *>(buf.data()), buf.size());
    ofs.flush();
  }
  catch (const std::exception &e) {
    wllog_fatal("failed to serialize {},{}\n", path.native(), e.what());
  }
}

template <typename obj_type>
void deserialize(const std::filesystem::path &path, obj_type &obj)
{
  try {
    wllog_info("loading from {}\n", path.native());
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
      wllog_fatal("failed to open {}\n", path.native());
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
      close(fd);
      wllog_fatal("failed to fstat {}\n", path.native());
    }

    void *addr = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) {
      wllog_fatal("failed to mmap {}\n", path.native());
    }

    zpp::bits::in in(std::span{static_cast<std::byte *>(addr),
                               static_cast<size_t>(sb.st_size)});

    in(obj).or_throw();
    munmap(addr, sb.st_size);
  }
  catch (const std::exception &e) {
    wllog_fatal("failed to deserialize {},{}\n", path.native(), e.what());
  }
}
