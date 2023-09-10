#include <iostream>
#include <cassert>
#include <atomic>

#include "lock_free_linklist.hpp"

template<typename K, typename V>
class LockFreeHashMap {

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

    uint32_t size;
    uint32_t capacity;
    LockFreeLinklist<K, V>* linkset;
    LockFreeStack<DeleteNode>* remove_set;
    LockFreeMemoryPool<Node>* pool;
    EpochManager* epoch;

private:
    int hash(const K& key) const {
        return std::hash<K>()(key) % size;
    }

public:

    LockFreeHashMap() = delete;

    LockFreeHashMap(const LockFreeHashMap&) = delete;

    LockFreeHashMap(const LockFreeHashMap&&) = delete;

    LockFreeHashMap& operator = (const LockFreeHashMap&) = delete;

    LockFreeHashMap& operator = (const LockFreeHashMap&&) = delete;

    explicit LockFreeHashMap(uint32_t _size) {
        size = _size;
        capacity = _size * 3;
        remove_set = new LockFreeStack<DeleteNode>(capacity);
        pool = new LockFreeMemoryPool<Node>(capacity);
        epoch = new EpochManager;
        linkset = new LockFreeLinklist<K, V>[size](pool, remove_set, epoch);
    }

    ~LockFreeHashMap() {
        delete remove_set;
        delete pool;
        delete epoch;
        delete linkset;
    }

    void insert(const K& key, const V& value) {
        int index = hash(key);
        LockFreeLinklist<K, V>* link = &linkset[index];
        link->insert(key, value);
    }

    V get(const K& key) {
        int index = hash(key);
        LockFreeLinklist<K, V>* link = &linkset[index];
        return link->search(key);
    }

    void remove(const K& key) {
        int index = hash(key);
        LockFreeLinklist<K, V>* link = &linkset[index];
        link->remove(key);
    }

};