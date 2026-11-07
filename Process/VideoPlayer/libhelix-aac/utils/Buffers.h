
#pragma once

#include <limits.h>         /* For INT_MAX */
#include "utils/helix_log.h"
#include "utils/Vector.h"

#ifndef MIN
#  define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#  define MAX(a,b) (((a)>(b))?(a):(b))
#endif


/**
 * @defgroup buffers Buffers
 * @ingroup tools
 * @brief Different Buffer Implementations
 */

namespace libhelix {

/**
 * @brief Shared functionality of all buffers
 * @ingroup buffers
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
template <typename T>
class BaseBuffer {
 public:
  BaseBuffer() = default;
  virtual ~BaseBuffer() = default;
  BaseBuffer(BaseBuffer const &) = delete;
  BaseBuffer &operator=(BaseBuffer const &) = delete;

  /// reads a single value
  virtual T read() = 0;

  /// reads multiple values
  virtual int readArray(T data[], int len) {
    if (data==nullptr){
      LOGE_HELIX("NPE");
      return 0;
    }
    int lenResult = MIN(len, available());
    for (int j = 0; j < lenResult; j++) {
      data[j] = read();
    }
    LOGD_HELIX("readArray %d -> %d", len, lenResult);
    return lenResult;
  }

  /// Removes the next len entries 
  virtual int clearArray(int len) {
    int lenResult = MIN(len, available());
    for (int j = 0; j < lenResult; j++) {
      read();
    }
    return lenResult;
  }


  /// Fills the buffer data 
  virtual int writeArray(const T data[], int len) {
    // LOGD_HELIX("%s: %d", LOG_METHOD, len);
    // CHECK_MEMORY();

    int result = 0;
    for (int j = 0; j < len; j++) {
      if (!write(data[j])) {
        break;
      }
      result = j + 1;
    }
    // CHECK_MEMORY();
    LOGD_HELIX("writeArray %d -> %d", len, result);
    return result;
  }


  /// Fills the buffer data and overwrites the oldest data if the buffer is full
  virtual int writeArrayOverwrite(const T data[], int len) {
    int to_delete = len - availableForWrite();
    if (to_delete>0){
      clearArray(to_delete);
    }
    return writeArray(data, len);
  }


  /// reads multiple values for array of 2 dimensional frames
  int readFrames(T data[][2], int len) {
    LOGD_HELIX("%s: %d", LOG_METHOD, len);
    // CHECK_MEMORY();
    int result = MIN(len, available());
    for (int j = 0; j < result; j++) {
      T sample = read();
      data[j][0] = sample;
      data[j][1] = sample;
    }
    // CHECK_MEMORY();
    return result;
  }

  template <int rows, int channels>
  int readFrames(T (&data)[rows][channels]) {
    int lenResult = MIN(rows, available());
    for (int j = 0; j < lenResult; j++) {
      T sample = read();
      for (int i = 0; i < channels; i++) {
        // data[j][i] = htons(sample);
        data[j][i] = sample;
      }
    }
    return lenResult;
  }

  /// peeks the actual entry from the buffer
  virtual T peek() = 0;

  /// checks if the buffer is full
  virtual bool isFull() = 0;

  bool isEmpty() { return available() == 0; }

  /// write add an entry to the buffer
  virtual bool write(T data) = 0;

  /// clears the buffer
  virtual void reset() = 0;

  ///  same as reset
  void clear() { reset(); }

  /// provides the number of entries that are available to read
  virtual int available() = 0;

  /// provides the number of entries that are available to write
  virtual int availableForWrite() = 0;

  /// returns the address of the start of the physical read buffer
  virtual T *address() = 0;

  virtual size_t size() = 0;

 protected:
  void setWritePos(int pos){};
};

/**
 * @brief A simple Buffer implementation which just uses a (dynamically sized)
 * array
 * @ingroup buffers
 * @author Phil Schatzmann
 * @copyright GPLv3
 */

template <typename T>
class SingleBuffer : public BaseBuffer<T> {
 public:
  /**
   * @brief Construct a new Single Buffer object
   *
   * @param size
   */
  SingleBuffer(int size) {
    this->max_size = size;
    buffer.resize(max_size);
    reset();
  }

  /**
   * @brief Construct a new Single Buffer w/o allocating any memory
   */
  SingleBuffer() { reset(); }

  /// notifies that the external buffer has been refilled
  void onExternalBufferRefilled(void *data, int len) {
    this->owns_buffer = false;
    this->buffer = (uint8_t *)data;
    this->current_read_pos = 0;
    this->current_write_pos = len;
  }

  bool write(T sample) override {
    bool result = false;
    if (current_write_pos < max_size) {
      buffer[current_write_pos++] = sample;
      result = true;
    }
    return result;
  }

  T read() override {
    T result = 0;
    if (current_read_pos < current_write_pos) {
      result = buffer[current_read_pos++];
    }
    return result;
  }

  T peek() override {
    T result = 0;
    if (current_read_pos < current_write_pos) {
      result = buffer[current_read_pos];
    }
    return result;
  }

  int available() override {
    int result = current_write_pos - current_read_pos;
    return MAX(result, 0);
  }

  int availableForWrite() override { return max_size - current_write_pos; }

  bool isFull() override { return availableForWrite() <= 0; }

  /// consumes len bytes and moves current data to the beginning
  int clearArray(int len) override{
    if (len<=0) return 0;
    int len_available = available();
    if (len>available()) {
      reset();
      return len_available;
    }
    current_read_pos += len;
    len_available -= len;
    memmove(buffer.data(), buffer.data()+current_read_pos, len_available);
    current_read_pos = 0;
    current_write_pos = len_available;

    if (is_clear_with_zero){
      memset(buffer.data()+current_write_pos,0,buffer.size()-current_write_pos);
    }

    return len;
  }

  /// Provides address to beginning of the buffer
  T *address() override { return buffer.data(); }

  /// Provides address of actual data
  T *data() { return buffer.data()+current_read_pos; }

  void reset() override {
    current_read_pos = 0;
    current_write_pos = 0;
    if (is_clear_with_zero){
      memset(buffer.data(),0,buffer.size());
    }
  }

  /// If we load values directly into the address we need to set the avialeble
  /// size
  size_t setAvailable(size_t available_size) {
    size_t result = MIN(available_size, (size_t) max_size);
    current_read_pos = 0;
    current_write_pos = result;
    return result;
  }


  size_t size() override { return max_size; }

  void resize(int size) {
    if (buffer.size() != size) {
      buffer.resize(size);
      max_size = size;
    }
  }

  /// Sets the buffer to 0 on clear
  void setClearWithZero(bool flag){
    is_clear_with_zero = flag;
  }

 protected:
  int max_size = 0;
  int current_read_pos = 0;
  int current_write_pos = 0;
  bool owns_buffer = true;
  bool is_clear_with_zero = false;
  Vector<T> buffer{0};

  void setWritePos(int pos) { current_write_pos = pos; }
};


}  // ns
