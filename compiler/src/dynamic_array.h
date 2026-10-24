// ============================================================================
// dynamic_array.h - our own growable array (DSA): used to store the TOKENS
// ----------------------------------------------------------------------------
//   * starts with capacity 10, DOUBLES when full (geometric growth)
//   * pushBack is amortised O(1): to push n items the copies made by all the
//     resizes are 10 + 20 + 40 + ... < 2n   (aggregate method)
//   * operator[] is O(1) random access - the parser needs peek(k)
//   * Rule of Three: copying is deleted so two arrays can never free the same
//     block; moving transfers ownership
// It also records its own growth (capacity history, element copies) so the
// front end can show the amortised analysis on the user's real program.
// ============================================================================
#pragma once
#include <cstddef>
#include <stdexcept>
#include <vector>

template <typename T>
class DynamicArray {
public:
    DynamicArray() : data_(new T[10]), size_(0), capacity_(10) { history_.push_back(10); }
    ~DynamicArray() { delete[] data_; }
    DynamicArray(const DynamicArray&) = delete;
    DynamicArray& operator=(const DynamicArray&) = delete;
    DynamicArray(DynamicArray&& o) noexcept
        : data_(o.data_), size_(o.size_), capacity_(o.capacity_), copies_(o.copies_), history_(std::move(o.history_)) {
        o.data_ = nullptr; o.size_ = o.capacity_ = 0;
    }
    DynamicArray& operator=(DynamicArray&& o) noexcept {
        if (this != &o) {
            delete[] data_;
            data_ = o.data_; size_ = o.size_; capacity_ = o.capacity_; copies_ = o.copies_; history_ = std::move(o.history_);
            o.data_ = nullptr; o.size_ = o.capacity_ = 0;
        }
        return *this;
    }

    void pushBack(const T& value) {
        if (size_ == capacity_) resize();          // full -> double, O(size) once in a while
        data_[size_++] = value;                    // the common O(1) case
    }
    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    T& at(size_t i) { if (i >= size_) throw std::out_of_range("DynamicArray index"); return data_[i]; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    T& back() { return data_[size_ - 1]; }
    void clear() { delete[] data_; data_ = new T[10]; size_ = 0; capacity_ = 10; copies_ = 0; history_.assign(1, 10); }
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    // statistics for the amortised-analysis view
    size_t elementCopies() const { return copies_; }
    const std::vector<size_t>& capacityHistory() const { return history_; }

private:
    void resize() {
        size_t newCap = capacity_ ? capacity_ * 2 : 10;   // 1. double the capacity
        T* fresh = new T[newCap];                        // 2. allocate a new block on the heap
        for (size_t i = 0; i < size_; i++)               // 3. copy the old elements
            fresh[i] = data_[i];
        copies_ += size_;
        delete[] data_;                                  // 4. free the old block
        data_ = fresh;                                   // 5. point to the new block
        capacity_ = newCap;
        history_.push_back(newCap);
    }

    T* data_;
    size_t size_, capacity_;
    size_t copies_ = 0;
    std::vector<size_t> history_;
};
