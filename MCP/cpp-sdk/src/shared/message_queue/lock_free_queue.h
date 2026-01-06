/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * LockFreeSPSCQueue Function Description
 * High-performance lock-free single producer single consumer queue for inter-thread communication
 *
 * Notes:
 * - Only supports single producer and single consumer, not thread-safe in multi-threaded scenarios
 * - Uses atomic operations for thread safety, avoiding locks
 * - Capacity must be power of 2 and at least 2
 * - Uses cache-line alignment to prevent false sharing
 * - Supports any copyable data type
 */

#ifndef MCP_LOCK_FREE_QUEUE_INCLUDE_H_
#define MCP_LOCK_FREE_QUEUE_INCLUDE_H_

#include <atomic>
#include <cstddef>
#include <thread>

#include "shared/common_type.h"
#include "mcp_log.h"

namespace Mcp {

constexpr unsigned int MCP_LFQ_MIN_CAPACITY = (MAX_THREAD_NUM * 2);
constexpr unsigned int MCP_LFQ_CACHELINE_SIZE = 64U;
constexpr unsigned int MCP_LFQ_ALIGN_MASK = (MCP_LFQ_CACHELINE_SIZE - 1U);
constexpr unsigned int MCP_LFQ_ALIGN_PADDING = MCP_LFQ_ALIGN_MASK;

/**
 * @brief Lock-free single producer single consumer queue
 * Lock-free implementation based on ring buffer for high-performance inter-thread communication
 */
template <typename T>
class LockFreeSPSCQueue {
public:
    LockFreeSPSCQueue() : size_(0), mask_(0), buffer_(nullptr) {}

    /**
     * @brief Constructor with runtime configuration
     * @param size Queue capacity (must be power of 2 and at least 2)
     */
    LockFreeSPSCQueue(size_t size) : size_(0), mask_(0), buffer_(nullptr)
    {
        // Validate size
        if (size < MCP_LFQ_MIN_CAPACITY) {
            size = MCP_LFQ_MIN_CAPACITY;
        }
        // Ensure power of 2
        if ((size & (size - 1)) != 0) {
            size_t nextPower = MCP_LFQ_MIN_CAPACITY;
            while (nextPower < size) {
                nextPower <<= 1;
            }
            size = nextPower;
        }

        size_ = size;
        mask_ = size_ - 1;

        // Allocate aligned buffer
        bufferMemory_ = std::make_unique<char[]>(sizeof(Node) * size_ + MCP_LFQ_ALIGN_MASK);
        buffer_ = reinterpret_cast<Node*>((reinterpret_cast<uintptr_t>(bufferMemory_.get()) +
            MCP_LFQ_ALIGN_MASK) & ~static_cast<uintptr_t>(MCP_LFQ_ALIGN_MASK));
    }

    ~LockFreeSPSCQueue() = default;

    // Non-copyable and non-movable
    LockFreeSPSCQueue(const LockFreeSPSCQueue&) = delete;
    LockFreeSPSCQueue& operator=(const LockFreeSPSCQueue&) = delete;
    LockFreeSPSCQueue(LockFreeSPSCQueue&&) = delete;
    LockFreeSPSCQueue& operator=(LockFreeSPSCQueue&&) = delete;

    /**
     * @brief Producer writes data
     * @param item Data to write
     * @return true if write successful (false when queue is full)
     */
    bool Push(const T& item)
    {
        if (!buffer_)
            return false; // Not initialized

        size_t currentWrite = writePos.load(std::memory_order_relaxed);
        size_t nextWrite = (currentWrite + 1) & mask_;

        if (nextWrite == readPos.load(std::memory_order_acquire)) {
            return false; // Queue full
        }

        buffer_[currentWrite].data = item;
        writePos.store(nextWrite, std::memory_order_release);
        return true;
    }

    /**
     * @brief Consumer reads data
     * @param item Output parameter to store read data
     * @return true if read successful (false when queue is empty)
     */
    bool Pop(T& item)
    {
        if (!buffer_) return false;  // Not initialized

        size_t currentRead = readPos.load(std::memory_order_relaxed);
        if (currentRead == writePos.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }

        item = std::move(buffer_[currentRead].data);
        readPos.store((currentRead + 1) & mask_, std::memory_order_release);
        return true;
    }

    /**
     * @brief Get current element count in queue
     * @return Number of elements
     */
    size_t Size() const
    {
        if (!buffer_) return 0;  // Not initialized
        size_t writeValue = writePos.load(std::memory_order_relaxed);
        size_t readValue = readPos.load(std::memory_order_relaxed);
        return (writeValue - readValue) & mask_;
    }

    /**
     * @brief Check if queue is empty
     * @return true if empty
     */
    bool Empty() const
    {
        return !buffer_ || (writePos.load(std::memory_order_relaxed) == readPos.load(std::memory_order_relaxed));
    }

    /**
     * @brief Check if queue is full
     * @return true if full
     */
    bool Full() const
    {
        if (!buffer_) return false;  // Not initialized
        size_t nextWrite = (writePos.load(std::memory_order_relaxed) + 1) & mask_;
        return nextWrite == readPos.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get queue capacity
     * @return Queue capacity
     */
    size_t Capacity() const { return size_; }

private:
    struct Node {
        alignas(MCP_LFQ_CACHELINE_SIZE) T data;  // Cache line aligned to avoid false sharing
    };

    // Runtime configuration
    size_t size_;
    size_t mask_;  // size_ - 1 for power of 2 optimization

    // Dynamic buffer allocation
    std::unique_ptr<char[]> bufferMemory_;
    Node* buffer_;

    alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<size_t> writePos{0};
    alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<size_t> readPos{0};
};

/**
 * @brief MPSC queue with bounded capacity using ring buffer
 *
 * Alternative implementation using a ring buffer for better cache performance
 * when the maximum size is known in advance.
 */
template <typename T>
class MPSCQueue {
public:
    /**
     * @brief Constructor
     * @param capacity Maximum capacity (must be power of 2)
     */
    explicit MPSCQueue(size_t capacity)
    {
        // Ensure capacity is power of 2
        if (capacity < MCP_LFQ_MIN_CAPACITY) {
            capacity = MCP_LFQ_MIN_CAPACITY;
        }

        if ((capacity & (capacity - 1)) != 0) {
            size_t nextPower = MCP_LFQ_MIN_CAPACITY;
            while (nextPower < capacity) {
                nextPower <<= 1;
            }
            capacity = nextPower;
        }

        capacity_ = capacity;
        mask_ = capacity_ - 1;

        // Allocate buffer
        buffer_ = std::make_unique<Node[]>(capacity_);
    }

    ~MPSCQueue()
    {
        // Clean up remaining items
        T* item;
        while (!Empty()) {
            size_t tail = tail_.fetch_add(1);
            Node& node = buffer_[tail & mask_];
            item = node.data.exchange(nullptr, std::memory_order_acq_rel);
            if (item != nullptr) {
                delete item;
            }
            size_.fetch_sub(1);
        }
    }

    // Non-copyable and non-movable
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    MPSCQueue(MPSCQueue&&) = delete;
    MPSCQueue& operator=(MPSCQueue&&) = delete;

    /**
     * @brief Push an element (thread-safe for multiple producers)
     * @param item Item to push
     * @return true if successful, false if queue is full
     */
    bool Push(const T& item)
    {
        if (Size() >= capacity_) {
            MCP_LOG(MCP_LOG_LEVEL_ERROR, "MPSCQueue overflow detected, capacity=%zu", capacity_);
            return false;
        }
    
        size_t pos = head_.fetch_add(1, std::memory_order_acq_rel);
        size_t index = pos & mask_;

        Node& node = buffer_[index];

        // Wait if this slot is still being consumed
        while (node.ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // Store the data
        node.data.store(new T(item), std::memory_order_release);
        node.ready.store(true, std::memory_order_release);
        size_.fetch_add(1);

        return true;
    }

    /**
     * @brief Try to pop an element (NOT thread-safe for multiple consumers)
     * @param result Reference to store the result
     * @return true if successful, false if empty
     */
    bool TryPop(T& result)
    {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t index = tail & mask_;
        Node& node = buffer_[index];

        if (!node.ready.load(std::memory_order_acquire)) {
            return false;
        }

        // Get and delete the data
        T* data = node.data.exchange(nullptr, std::memory_order_acq_rel);
        node.ready.store(false, std::memory_order_release);

        if (data) {
            result = std::move(*data);
            delete data;

            // Move to next position
            tail_.fetch_add(1);
            size_.fetch_sub(1);
            return true;
        }

        return false;
    }

    /**
     * @brief Get current size
     * @return Approximate number of elements
     */
    size_t Size() const
    {
        return size_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if empty
     * @return true if empty
     */
    bool Empty() const
    {
        return size_.load(std::memory_order_acquire) == 0;
    }

    /**
     * @brief Get capacity
     * @return Maximum capacity
     */
    size_t Capacity() const
    {
        return capacity_;
    }

private:
    struct Node {
        alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<T*> data{nullptr};
        alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<bool> ready{false};
    };

    // Ring buffer storage
    std::unique_ptr<Node[]> buffer_;
    size_t capacity_;
    size_t mask_;

    // Producer positions
    alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<size_t> head_{0};

    // Consumer position (only modified by consumer)
    alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<size_t> tail_{0};

    alignas(MCP_LFQ_CACHELINE_SIZE) std::atomic<size_t> size_{0};
};

} // namespace Mcp

#endif // MCP_LOCK_FREE_QUEUE_INCLUDE_H_
