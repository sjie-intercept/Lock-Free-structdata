#include <iostream>
#include <atomic>
#include <cassert>

template<typename T>
class LockFreeRingBuffer {

private:

    struct Node {
        T data;
        std::atomic<uint32_t> seq;
    };
    Node *buffer;
    std::atomic<uint32_t> enqueue_pos;
    std::atomic<uint32_t> dequeue_pos;
    uint32_t size;

public:
    LockFreeRingBuffer() = delete;

    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;

    LockFreeRingBuffer(const LockFreeRingBuffer&&) = delete;

    LockFreeRingBuffer& operator = (const LockFreeRingBuffer&) = delete;
    
    LockFreeRingBuffer& operator = (const LockFreeRingBuffer&&) = delete;

    //SIZE must be pow of 2 minus 1 
    explicit LockFreeRingBuffer(uint32_t SIZE) {
        assert(__builtin_popcount(SIZE + 1) == 1);
        size = SIZE;
        buffer = new (std::nothrow) Node[size + 1];
        if (!buffer) {
            std::cerr << "Memory allocation failed for lock free buffer\n";
            exit(0);
        }
        enqueue_pos = 0;
        dequeue_pos = 0;
        for (int i = 0; i <= size; ++i) {
            buffer[i].seq = i;
        }
    }

    ~LockFreeRingBuffer() {
        delete[] buffer;
    }

    bool enqueue(const T& val) {
        Node *node;
        uint32_t pos;
        do {
            pos = enqueue_pos.load(std::memory_order_relaxed);
            node = &buffer[pos & size];
            uint32_t seq = node->seq.load(std::memory_order_acquire);
            if (seq + size == pos) {
                return false;
            }
        } while (enqueue_pos.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed) == false);
        node->data = val;
        node->seq.store(pos + 1, std::memory_order_release);
        return true;
    }
    
    bool dequeue(T &val) {
        Node *node;
        uint32_t pos;
        do {
            pos = dequeue_pos.load(std::memory_order_relaxed);
            node = &buffer[pos & size];
            uint32_t seq = node->seq.load(std::memory_order_acquire);
            if (seq == pos) {
                return false;
            }
        } while (dequeue_pos.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed) == false);
        val = node->data;
        node->seq.store(pos + size + 1, std::memory_order_release);
        return true;
    }
};