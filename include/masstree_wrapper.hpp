#pragma once

#include "masstree/config.h"

#include "masstree/compiler.hh"
#include "masstree/kvthread.hh"
#include "masstree/masstree.hh"
#include "masstree/masstree_insert.hh"
#include "masstree/masstree_print.hh"
#include "masstree/masstree_remove.hh"
#include "masstree/masstree_scan.hh"
#include "masstree/masstree_stats.hh"
#include "masstree/masstree_tcursor.hh"
#include "masstree/string.hh"

class key_unparse_unsigned {
public:
    static int unparse_key(Masstree::key<uint64_t> key, char* buf, int buflen) {
        return snprintf(buf, buflen, "%" PRIu64, key.ikey());
    }
};

template<typename T>
class MasstreeWrapper {
public:
    struct table_params : public Masstree::nodeparams<15, 15> {
        using value_type = T*;
        using value_print_type = Masstree::value_print<value_type>;
        using threadinfo_type = threadinfo;
        using key_unparse_type = key_unparse_unsigned;
    };

    using Str = Masstree::Str;
    using table_type = Masstree::basic_table<table_params>;
    using unlocked_cursor_type = Masstree::unlocked_tcursor<table_params>;
    using cursor_type = Masstree::tcursor<table_params>;
    using leaf_type = Masstree::leaf<table_params>;
    using internode_type = Masstree::internode<table_params>;

    using node_type = typename table_type::node_type;
    using nodeversion_value_type = typename unlocked_cursor_type::nodeversion_value_type;

    static __thread typename table_params::threadinfo_type *ti;

    MasstreeWrapper() {
        this->table_init();
    }

    void table_init() {
        if (ti == nullptr)
            ti = threadinfo::make(threadinfo::TI_MAIN, -1);
        table_.initialize(*ti);
    }

    static void thread_init(int thread_id) {
        if (ti == nullptr)
            ti = threadinfo::make(threadinfo::TI_PROCESS, thread_id);
    }

    bool insert_value(const char* key, std::size_t len_key, T* value) {
        cursor_type lp(table_, key, len_key);
        bool found = lp.find_insert(*ti);

        if (found) {
            lp.finish(0, *ti); // release lock
            return 0; // not inserted
        }

        lp.value() = value;
        fence();
        lp.finish(1, *ti); // finish insert
        return 1; // inserted
    }

    bool insert_value(std::string_view key, T *value) {
        return insert_value(key.data(), key.size(), value);
    }

    bool update_value(const char* key, std::size_t len_key, T* value) {
        cursor_type lp(table_, key, len_key);
        bool found = lp.find_locked(*ti); // lock a node which potentailly contains the value

        if (found) {
            lp.value() = value;
            fence();
            lp.finish(0, *ti); // release lock
            return 1; // updated
        }

        lp.finish(0, *ti); // release lock
        return 0; // not updated
    }

    bool update_value(std::string_view key, T *value) {
        return update_value(key.data(), key.size(), value);
    }

    bool remove_value(const char* key, std::size_t len_key) {
        cursor_type lp(table_, key, len_key);
        bool found = lp.find_locked(*ti); // lock a node which potentailly contains the value

        if (found) {
            lp.finish(-1, *ti); // finish remove
            return 1; // removed
        }

        lp.finish(0, *ti); // release lock
        return 0; // not removed
    }

    bool remove_value(std::string_view key) {
        return remove_value(key.data(), key.size());
    }

    const T* get_value(const char* key, std::size_t len_key) {
        unlocked_cursor_type lp(table_, key, len_key);
        bool found = lp.find_unlocked(*ti);
        if (found) {
            return lp.value();
        }
        return nullptr;
    }

    const T* get_value(std::string_view key) {
        return get_value(key.data(), key.size());
    }

private:
    table_type table_;
};

template<typename T>
__thread typename MasstreeWrapper<T>::table_params::threadinfo_type *
        MasstreeWrapper<T>::ti = nullptr;
#ifdef GLOBAL_VALUE_DEFINE
volatile mrcu_epoch_type active_epoch = 1;
volatile std::uint64_t globalepoch = 1;
volatile bool recovering = false;
#else
extern volatile mrcu_epoch_type active_epoch;
extern volatile std::uint64_t globalepoch;
extern volatile bool recovering;
#endif