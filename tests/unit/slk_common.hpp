#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <fmt/format.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "slk/streams.hpp"

namespace slk {

/// Class used for SLK tests. It creates a `slk::Builder` that can be written
/// to. After you have written the data to the builder, you can get a
/// `slk::Reader` and try to decode the encoded data.
class Loopback {
 public:
  ~Loopback() {
    CHECK(builder_) << "You haven't created a builder!";
    CHECK(reader_) << "You haven't created a reader!";
    reader_->Finalize();
  }

  slk::Builder *GetBuilder() {
    CHECK(!builder_) << "You have already allocated a builder!";
    builder_ = std::make_unique<slk::Builder>(
        [this](const uint8_t *data, size_t size, bool have_more) {
          Write(data, size, have_more);
        });
    return builder_.get();
  }

  slk::Reader *GetReader() {
    CHECK(builder_) << "You must first get a builder before getting a reader!";
    CHECK(!reader_) << "You have already allocated a reader!";
    builder_->Finalize();
    auto ret = slk::CheckStreamComplete(data_.data(), data_.size());
    CHECK(ret.status == slk::StreamStatus::COMPLETE);
    CHECK(ret.stream_size == data_.size());
    size_ = ret.encoded_data_size;
    Dump();
    reader_ = std::make_unique<slk::Reader>(data_.data(), data_.size());
    return reader_.get();
  }

  size_t size() { return size_; }

 private:
  void Write(const uint8_t *data, size_t size, bool have_more) {
    for (size_t i = 0; i < size; ++i) {
      data_.push_back(data[i]);
    }
  }

  void Dump() {
    std::string dump;
    for (size_t i = 0; i < data_.size(); ++i) {
      dump += fmt::format("{:02x}", data_[i]);
      if (i != data_.size() - 1) {
        dump += " ";
      }
    }
    // This stores the encoded SLK stream into the test XML output. To get the
    // data you have to specify to the test (during runtime) that it should
    // create an XML output.
    ::testing::Test::RecordProperty("slk_stream", dump);
  }

  std::vector<uint8_t> data_;
  std::unique_ptr<slk::Builder> builder_;
  std::unique_ptr<slk::Reader> reader_;
  size_t size_{0};
};

}  // namespace slk
