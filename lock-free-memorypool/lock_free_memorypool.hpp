#include <iostream>
#include <cassert>
#include <atomic>

template<typename T>
class LockFreeMemoryPool {

private:
    struct Node {
        T data;
        Node* next;
    };

    Node* pool;
    uint32_t pool_size;

    std::atomic<uint64_t> top;
    std::atomic<bool>* allocated;

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

    LockFreeMemoryPool() = delete;

    LockFreeMemoryPool(const LockFreeMemoryPool&) = delete;

    LockFreeMemoryPool(const LockFreeMemoryPool&&) = delete;

    LockFreeMemoryPool& operator = (const LockFreeMemoryPool&) = delete;

    LockFreeMemoryPool& operator = (const LockFreeMemoryPool&&) = delete;

    explicit LockFreeMemoryPool(uint32_t size) {
        pool_size = size;
        pool = new Node[size];
        allocated = new std::atomic<bool>[size];
        for (int i = 0; i < size - 1; ++i) {
            pool[i].next = &pool[i + 1];
            allocated[i].store(false);
        }
        pool[size - 1].next = nullptr;
        allocated[size - 1].store(false);
        top.store(pack(pool, 0));
    }

    ~LockFreeMemoryPool() {
        delete[] pool;
        delete[] allocated;
    }

    T* allocate() {
        uint64_t old_top;
        uint64_t nex_top;
        Node* node;
        do {
            old_top = top.load(std::memory_order_acquire);
            node = unpackPtr(old_top);
            if (node == nullptr) return nullptr;
            nex_top = pack(node->next, unpackVersion(old_top) + 1);
        } while (top.compare_exchange_strong(old_top, nex_top, std::memory_order_acq_rel) == false);

        uint32_t index = node - pool;
        allocated[index].store(true, std::memory_order_release);
        return &node->data;
    }

    void deallocate(T* ptr) {
        Node* node = reinterpret_cast<Node*>(ptr);
        uint32_t index = node - pool;
        bool status = true;

        if (node < pool || index >= pool_size) return;
        if (allocated[index].compare_exchange_strong(status, false, std::memory_order_acq_rel) == false) return;

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