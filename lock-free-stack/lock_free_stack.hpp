#include <iostream>
#include <cassert>
#include <atomic>

#include "../lock-free-memorypool/lock_free_memorypool.hpp"

template<typename T>
class LockFreeStack {

private:
    struct Node {
        T data;
        Node* next;
    };

    std::atomic<uint64_t> top;
    LockFreeMemoryPool<Node>* pool;

    static constexpr uint64_t ptr_mask = 0x0000ffffffffffff;

private:

    uint64_t pack(Node* node, uint16_t version) {
        uint64_t v = version;
        uint64_t ptr = reinterpret_cast<uint64_t>(node);
        return (v << 48) | (ptr & ptr_mask);
    }

    Node* unpackPtr(uint64_t combined) {
        return reinterpret_cast<Node*>(combined & ptr_mask);
    }

    uint16_t unpackVersion(uint64_t combined) {
        return combined >> 48;
    }

public:

    LockFreeStack() = delete;

    LockFreeStack(const LockFreeStack&) = delete;

    LockFreeStack(const LockFreeStack&&) = delete;

    LockFreeStack& operator = (const LockFreeStack&) = delete;

    LockFreeStack& operator = (const LockFreeStack&&) = delete;

    explicit LockFreeStack(uint32_t size):top(pack(nullptr, 0)), pool(new LockFreeMemoryPool<Node>(size)) {};

    bool pop(T& val) {
        uint64_t old_top;
        uint64_t nex_top;
        Node* node;
        do {
            old_top = top.load(std::memory_order_acquire);
            node = unpackPtr(old_top);
            if (node == nullptr) return false;
            nex_top = pack(node->next, unpackVersion(old_top) + 1);
        } while (top.compare_exchange_strong(old_top, nex_top, std::memory_order_acq_rel) == false);
        val = node->data;
        pool->deallocate(node);
        return true;
    }

    void push(const T& val) {
        Node* node = pool->allocate();
        if (node == nullptr) {
            std::cerr << "Pool size is too small\n";
            exit(0);
        }
        node->data = val;

        uint64_t new_top, cur_top;
        uint16_t v;

        do {
            cur_top = top.load(std::memory_order_acquire);
            v = unpackVersion(cur_top) + 1;
            node->next = unpackPtr(cur_top);
            new_top = pack(node, v);
        } while (top.compare_exchange_strong(cur_top, new_top, std::memory_order_acq_rel) == false);
    }

};