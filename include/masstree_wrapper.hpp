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
  static int unparse_key(Masstree::key<uint64_t> key, char *buf, int buflen) {
    return snprintf(buf, buflen, "%" PRIu64, key.ikey());
  }
};
template <typename T> class MasstreeWrapper {
public:
  struct table_params : public Masstree::nodeparams<15, 15> {
    using value_type = T *;
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
  using nodeversion_value_type =
      typename unlocked_cursor_type::nodeversion_value_type;

  static __thread typename table_params::threadinfo_type *ti;

  MasstreeWrapper() { this->table_init(); }

  void table_init() {
    if (ti == nullptr)
      ti = threadinfo::make(threadinfo::TI_MAIN, -1);
    table_.initialize(*ti);
  }

  static void thread_init(int thread_id) {
    if (ti == nullptr)
      ti = threadinfo::make(threadinfo::TI_PROCESS, thread_id);
  }

  bool insert_value(const char *key, std::size_t len_key, T *value) {
    cursor_type lp(table_, key, len_key);
    bool found = lp.find_insert(*ti);

    if (found) {
      lp.finish(0, *ti); // release lock
      return 0;          // not inserted
    }

    lp.value() = value;
    fence();
    lp.finish(1, *ti); // finish insert
    return 1;          // inserted
  }

  bool update_value(const char *key, std::size_t len_key, T *value) {
    cursor_type lp(table_, key, len_key);
    bool found =
        lp.find_locked(*ti); // lock a node which potentailly contains the value

    if (found) {
      lp.value() = value;
      fence();
      lp.finish(0, *ti); // release lock
      return 1;          // updated
    }

    lp.finish(0, *ti); // release lock
    return 0;          // not updated
  }

  bool remove_value(const char *key, std::size_t len_key) {
    cursor_type lp(table_, key, len_key);
    bool found =
        lp.find_locked(*ti); // lock a node which potentailly contains the value

    if (found) {
      lp.finish(-1, *ti); // finish remove
      return 1;           // removed
    }

    lp.finish(0, *ti); // release lock
    return 0;          // not removed
  }

  const T *get_value(const char *key, std::size_t len_key) {
    unlocked_cursor_type lp(table_, key, len_key);
    bool found = lp.find_unlocked(*ti);
    if (found) {
      return lp.value();
    }
    return nullptr;
  }
  class Callback {
  public:
    std::function<void(const leaf_type *, uint64_t)> per_node_func;
    std::function<void(const Str &, const T *)> per_kv_func;
  };
  class SearchRangeScanner {
  public:
    SearchRangeScanner(const char *const rkey, const std::size_t len_rkey,
                       const bool r_exclusive, Callback &callback,
                       int64_t max_scan_num = -1)
        : rkey_(rkey), len_rkey_(len_rkey), r_exclusive_(r_exclusive),
          callback_(callback), max_scan_num_(max_scan_num) {}

    template <typename ScanStackElt, typename Key>
    void visit_leaf(const ScanStackElt &iter, const Key &key, threadinfo &) {
      n_ = iter.node();
      v_ = iter.full_version_value();
      callback_.per_node_func(n_, v_);
    }

    bool visit_value(const Str key, T *val, threadinfo &) {
      if (max_scan_num_ >= 0 && scan_num_cnt_ >= max_scan_num_) {
        return false;
      }
      ++scan_num_cnt_;

      bool endless_key = (rkey_ == nullptr);

      if (endless_key) {
        callback_.per_kv_func(key, val);
        return true;
      }

      // compare key with end key
      const int res = memcmp(
          rkey_, key.s, std::min(len_rkey_, static_cast<std::size_t>(key.len)));

      bool smaller_than_end_key = (res > 0);
      bool same_as_end_key_but_shorter =
          ((res == 0) && (len_rkey_ > static_cast<std::size_t>(key.len)));
      bool same_as_end_key_inclusive =
          ((res == 0) && (len_rkey_ == static_cast<std::size_t>(key.len)) &&
           (!r_exclusive_));

      if (smaller_than_end_key || same_as_end_key_but_shorter ||
          same_as_end_key_inclusive) {
        callback_.per_kv_func(key, val);
        return true;
      }

      return false;
    }

  private:
    const char *const rkey_{};
    const std::size_t len_rkey_{};
    const bool r_exclusive_{};
    std::vector<const T *> scan_buffer_{};
    Callback &callback_;
    const bool limited_scan_{false};
    std::size_t scan_num_cnt_ = 0;
    const std::size_t max_scan_num_ = -1;

    leaf_type *n_;
    uint64_t v_;
  };

  void scan(const char *const lkey, const std::size_t len_lkey,
            const bool l_exclusive, const char *const rkey,
            const std::size_t len_rkey, const bool r_exclusive,
            Callback &&callback, int64_t max_scan_num = -1) {

    Str mtkey;
    if (lkey == nullptr) {
      mtkey = Str();
    } else {
      mtkey = Str(lkey, len_lkey);
    }

    SearchRangeScanner scanner(rkey, len_rkey, r_exclusive, callback,
                               max_scan_num);
    table_.scan(mtkey, !l_exclusive, scanner, *ti);
  }

  class BackwordScanner {
  public:
    BackwordScanner(const char *const lkey, const std::size_t len_lkey,
                    const bool l_exclusive, Callback &callback,
                    int64_t max_scan_num = -1)
        : lkey_(lkey), len_lkey_(len_lkey), l_exclusive_(l_exclusive),
          callback_(callback), max_scan_num_(max_scan_num) {}

    template <typename ScanStackElt, typename Key>
    void visit_leaf(const ScanStackElt &iter, const Key &key, threadinfo &) {
      n_ = iter.node();
      v_ = iter.full_version_value();
      callback_.per_node_func(n_, v_);
    }

    bool visit_value(const Str key, T *val, threadinfo &) {
      if (max_scan_num_ >= 0 && scan_num_cnt_ >= max_scan_num_) {
        return false;
      }
      ++scan_num_cnt_;

      bool endless_key = (lkey_ == nullptr);

      if (endless_key) {
        callback_.per_kv_func(key, val);
        return true;
      }

      // compare key with end key
      const int res = memcmp(
          lkey_, key.s, std::min(len_lkey_, static_cast<std::size_t>(key.len)));

      bool bigger_than_end_key = (res < 0);
      bool same_as_end_key_but_longer =
          ((res == 0) && (len_lkey_ < static_cast<std::size_t>(key.len)));
      bool same_as_end_key_inclusive =
          ((res == 0) && (len_lkey_ == static_cast<std::size_t>(key.len)) &&
           (!l_exclusive_));

      if (bigger_than_end_key || same_as_end_key_but_longer ||
          same_as_end_key_inclusive) {
        callback_.per_kv_func(key, val);
        return true;
      }

      return false;
    }

  private:
    const char *const lkey_{};
    const std::size_t len_lkey_{};
    const bool l_exclusive_{};
    std::vector<const T *> scan_buffer_{};
    Callback &callback_;
    std::size_t scan_num_cnt_ = 0;
    const std::size_t max_scan_num_ = -1;

    leaf_type *n_;
    uint64_t v_;
  };

  void rscan(const char *const lkey, const std::size_t len_lkey,
             const bool l_exclusive, const char *const rkey,
             const std::size_t len_rkey, const bool r_exclusive,
             Callback &&callback, int64_t max_scan_num = -1) {
    Str mtkey;
    if (lkey == nullptr) {
      mtkey = Str();
    } else {
      mtkey = Str(rkey, len_rkey);
    }

    BackwordScanner scanner(lkey, len_lkey, l_exclusive, callback,
                            max_scan_num);
    table_.rscan(mtkey, !r_exclusive, scanner, *ti);
  }

private:
  table_type table_;
};

template <typename T>
__thread typename MasstreeWrapper<T>::table_params::threadinfo_type
    *MasstreeWrapper<T>::ti = nullptr;
#ifdef GLOBAL_VALUE_DEFINE
volatile mrcu_epoch_type active_epoch = 1;
volatile std::uint64_t globalepoch = 1;
volatile bool recovering = false;
#else
extern volatile mrcu_epoch_type active_epoch;
extern volatile std::uint64_t globalepoch;
extern volatile bool recovering;
#endif