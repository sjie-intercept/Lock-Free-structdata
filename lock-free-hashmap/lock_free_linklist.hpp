#include <iostream>
#include <atomic>
#include <cassert>

#include "../lock-free-stack/lock_free_stack.hpp"

class EpochManager {

private:

    static constexpr int max_threads = 128;
    static constexpr uint64_t access = -1ull;
    std::atomic<uint64_t> globaepoch;
    std::atomic<uint64_t> localepoch[max_threads];


public:

    EpochManager() {
        globaepoch.store(0);
        for (int i = 0; i < max_threads; ++i) {
            localepoch[i].store(access);
        }
    }

    uint64_t get_epoch() {
        return globaepoch.fetch_add(1, std::memory_order_acq_rel);
    }

    int lockepoch() {
        uint64_t epoch = get_epoch();
        uint64_t is_access = access;
        for (int i = 0; i < max_threads; ++i) {
            if (localepoch[i].compare_exchange_strong(is_access, epoch, std::memory_order_acquire)) {
                return i;
            }
        }
        std::cerr << "threads is too much\n";
        exit(0);
        return -1;
    }

    void unlockepoch(int index) {
        localepoch[index].store(access, std::memory_order_release);
    }

    uint64_t minepoch() {
        uint64_t min_e = access; 
        for (int i = 0; i < max_threads; ++i) {
            uint64_t epoch = localepoch[i].load(std::memory_order_acquire);
            if (min_e > epoch) {
                min_e = epoch;
            }
        }
        return min_e;
    }

};

template<typename K, typename V>
class LockFreeLinklist {

private:

    struct Node {
        K key;
        V value;
        std::atomic<bool> changing;
        std::atomic<Node*> next;

    };

    struct DeleteNode {
        Node* node;
        uint64_t version;
    };

    //These resources come from outside, they need to be released manually by the upper application 
    LockFreeStack<DeleteNode>* remove_set;
    LockFreeMemoryPool<Node>* pool;
    EpochManager* epoch;

    Node head;

private :

    bool is_remove(Node* node) {
        return reinterpret_cast<uint64_t>(node->next.load(std::memory_order_acquire)) & 1;
    }

    Node* get_next(Node* node) {
        return reinterpret_cast<Node*>(reinterpret_cast<uint64_t>(node->next.load(std::memory_order_acquire)) & ~uint64_t(1));
    }

    void try_remove() {
        DeleteNode deletenode;
        while (remove_set.pop(deletenode)) {
            if (deletenode->version > epoch->minepoch()) {
                remove_set.push(deletenode);
                break;
            }
            else pool->deallocate(deletenode.node);
        }
    }

public:

    LockFreeLinklist() = delete;

    LockFreeLinklist(const LockFreeLinklist&) = delete;

    LockFreeLinklist(const LockFreeLinklist&&) = delete;

    LockFreeLinklist& operator = (const LockFreeLinklist&) = delete;

    LockFreeLinklist& operator = (const LockFreeLinklist&&) = delete;

    explicit LockFreeLinklist(LockFreeMemoryPool<Node>* _pool, LockFreeStack<DeleteNode>* _remove_set, EpochManager* _epoch) {
        head.next = nullptr;
        pool = _pool;
        remove_set = _remove_set;
        epoch = _epoch;
    }

    ~LockFreeLinklist() {
        Node* node = head.next.load(std::memory_order_acquire);
        DeleteNode deletenode;
        while (remove_set->pop(deletenode)) {
            pool->deallocate(deletenode.node);
        }
        while (node) {
            pool->deallocate(node);
            node = get_next(node);
        }
    }

    void insert(const K& key, const V& value) {
        try_remove();
        int index = epoch->lockepoch();

        Node* new_node = pool->allocate();
        if (new_node == nullptr) {
            std::cerr << "Pool size is too small\n";
            exit(0);
        }
        new_node->key = key;
        new_node->value = value;
        new_node->next.store(nullptr, std::memory_order_release);
        while (true) {
            Node* prev = &head;
            Node* node = prev->next.load(std::memory_order_acquire);
            while (node) {
                if (!is_remove(node) && node->key == key) {
                    break;
                }
                prev = node;
                node = get_next(node);
            }
            if (node != nullptr) {
                bool is_not_changing = false;
                if (node->changing.compare_exchange_strong(is_not_changing, true, std::memory_order_acq_rel)) {
                    node->value = value;
                    node->changing.store(is_not_changing, std::memory_order_release);
                }
                pool->deallocate(new_node);
            }
            if(prev->next.compare_exchange_strong(node, new_node, std::memory_order_acq_rel)) {
                break;
            }
        }

        epoch->unlockepoch(index);
    }

    V search(const K& key) {
        try_remove();
        int index = epoch->lockepoch();

        Node* node = head.next.load(std::memory_order_acquire);
        while (node) {
            if (!is_remove(node) && node->key == key) break;
            node = get_next(node);
        }
        V value;
        if (node != nullptr) value = node->value;
        epoch->unlockepoch(index);
        return value;
    }

    void remove(const K& key) {
        try_remove();
        int index = epoch->lockepoch();

        while (true) {
            Node* prev = &head;
            Node* node = prev->next.load(std::memory_order_acquire);
            while (node) {
                if (!is_remove(node) && node->key == key) break;
                prev = node;
                node = get_next(node);
            }
            if (node == nullptr) break;
            Node* next = get_next(node);
            Node* mark_next = reinterpret_cast<Node*>(reinterpret_cast<uint64_t>(next) | 1);
            if (!node->next.compare_exchange_strong(next, mark_next, std::memory_order_acq_rel)) {
                continue;
            }
            if (!prev->next.compare_exchange_strong(node, next, std::memory_order_acq_rel)) {
                node->next.store(next, std::memory_order_release);
                continue;
            }
            DeleteNode deletenode = {node, epoch->get_epoch()};
            remove_set->push(deletenode);
            break;
        }

        epoch->unlockepoch(index);
    }

};