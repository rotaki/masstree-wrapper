#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#define GLOBAL_VALUE_DEFINE
#include "masstree_wrapper.hpp"
#include "random.hpp"
#include "utils.hpp"

using MT = MasstreeWrapper<std::vector<uint8_t>>;

struct RandomKVs
{
    using KVPair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

    std::vector<KVPair> kvs;

    static std::vector<RandomKVs> generate(
        bool unique_keys,
        bool sorted,
        size_t partitions,
        size_t num_keys,
        size_t key_size,
        size_t val_min_size,
        size_t val_max_size)
    {
        std::vector<size_t> keys;

        if (unique_keys)
        {
            Permutation perm(0, num_keys - 1);
            for (size_t i = 0; i < num_keys; ++i)
            {
                keys.push_back(perm[i]);
            }
        }
        else
        {
            for (size_t i = 0; i < num_keys; ++i)
            {
                keys.push_back(urand_int(0, num_keys - 1));
            }
        }

        auto to_bytes = [&](size_t key) -> std::vector<uint8_t>
        {
            std::vector<uint8_t> key_vec(key_size, 0);
            for (size_t i = 0; i < std::min(sizeof(size_t), key_size); ++i)
            {
                key_vec[i] = static_cast<uint8_t>((key >> (8 * (sizeof(size_t) - 1 - i))) & 0xFF);
            }
            return key_vec;
        };

        std::vector<RandomKVs> result;
        result.reserve(partitions);

        size_t keys_per_partition = num_keys / partitions;

        for (size_t i = 0; i < partitions; ++i)
        {
            size_t start = i * keys_per_partition;
            size_t end = (i == partitions - 1) ? num_keys : (i + 1) * keys_per_partition;

            std::vector<KVPair> partition;
            partition.reserve(end - start);

            for (size_t idx = start; idx < end; ++idx)
            {
                size_t val_size = urand_int(val_min_size, val_max_size);
                std::vector<uint8_t> val(val_size);
                for (size_t j = 0; j < val_size; ++j)
                {
                    val[j] = static_cast<uint8_t>(urand_int(0, 255));
                }

                partition.emplace_back(to_bytes(keys[idx]), std::move(val));
            }

            if (sorted)
            {
                std::sort(partition.begin(), partition.end(), [](const KVPair &a, const KVPair &b)
                          { return a.first < b.first; });
            }

            result.emplace_back(RandomKVs{std::move(partition)});
        }

        return result;
    }

    auto begin() const { return kvs.begin(); }
    auto end() const { return kvs.end(); }
};

void measure_time(const char *title, const std::function<void()> &f)
{
    auto start = std::chrono::steady_clock::now();
    f();
    auto elapsed = std::chrono::steady_clock::now() - start;
    printf("%s: %lld ms\n", title,
           std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void run_insertion_test(MT& mt, const std::vector<RandomKVs>& partitions) {
    std::vector<std::thread> threads;
  
    auto insert_partition = [&mt](const RandomKVs& partition, size_t thread_id) {
      mt.thread_init(thread_id);  // Crucial step!
      for (const auto& [key, val] : partition.kvs) {
        // Make a proper copy of the value into Masstree storage
        // auto val_copy = new std::vector<uint8_t>(val);
        mt.insert_value(reinterpret_cast<const char*>(key.data()), key.size(), const_cast<std::vector<uint8_t>*>(&val));
      }
    };
  
    for (size_t i = 0; i < partitions.size(); ++i) {
      threads.emplace_back(insert_partition, std::cref(partitions[i]), i);
    }
  
    for (auto& t : threads) {
      t.join();
    }
}

void verify_insertion(MT &mt, const std::vector<RandomKVs> &partitions)
{
    size_t missing = 0;
    for (const auto &partition : partitions)
    {
        for (const auto &[key, val] : partition.kvs)
        {
            auto retrieved = mt.get_value(reinterpret_cast<const char *>(key.data()), key.size());
            if (!retrieved || *retrieved != val)
            {
                missing++;
            }
        }
    }
    if (missing == 0)
    {
        printf("All keys successfully verified.\n");
    }
    else
    {
        printf("Verification failed. Missing keys: %zu\n", missing);
    }
}

int main(int argc, char** argv) {
    size_t num_threads = argc > 1 ? std::stoul(argv[1]) : 3;
    size_t num_keys = argc > 2 ? std::stoull(argv[2]) : 1'000'000;
    size_t key_size = argc > 3 ? std::stoul(argv[3]) : 100;
    size_t val_min_size = argc > 4 ? std::stoul(argv[4]) : 50;
    size_t val_max_size = argc > 5 ? std::stoul(argv[5]) : 100;
  
    MT mt;
  
    printf("Generating random key-value pairs with %zu threads and %zu keys\n", num_threads, num_keys);
  
    auto kv_partitions = RandomKVs::generate(
        true, false, num_threads, num_keys, key_size, val_min_size, val_max_size);
  
    measure_time("Masstree Parallel Insertion", [&]() {
      run_insertion_test(mt, kv_partitions);
    });
  
    printf("Done.\n");
  
    return 0;
  }


// int main(int argc, char **argv)
// {
    // size_t num_threads = 3;
    // size_t num_keys = 1'000'000;
    // size_t key_size = 100;
    // size_t val_min_size = 50;
    // size_t val_max_size = 100;

    // MT mt;

    // printf("Generating random key-value pairs...\n");

    // auto kv_partitions = RandomKVs::generate(
        // true, false, num_threads, num_keys, key_size, val_min_size, val_max_size);

    // printf("Generated %zu partitions\n", kv_partitions.size());

    // printf("Starting insertion test...\n");

    // measure_time("Masstree Parallel Insertion", [&]()
                 // { run_insertion_test(mt, kv_partitions); });
    
    // printf("Starting verification test...\n");

    // printf("Verifying inserted keys...\n");

    // measure_time("Masstree Insertion Verification", [&]()
                 // { verify_insertion(mt, kv_partitions); });

    // printf("Done.\n");

    // return 0;
// }
