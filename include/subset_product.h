#ifndef S2_SUBSETPROD_H
#define S2_SUBSETPROD_H

#include <functional>
#include <vector>
#include <memory>
#include <NTL/ZZ.h>

#include <util.h>
#include <config.h>

/* TEMPLATE FUNCTIONS/CLASSES */

template <typename T, size_t N>
struct ArrayHasher
{
    size_t
    operator()(const std::array<T, N> &arr) const
    {
        size_t hash {0};
        for (auto v : arr)
            hash ^= std::hash<T>{}(v)  
                        + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};


template <size_t N>
struct ArrayHasher<NTL::ZZ, N>
{
    size_t
    operator()(const std::array<NTL::ZZ, N> &arr) const
    {
        size_t hash {0};
        for (auto v : arr)
            hash ^= std::hash<unsigned long>{}(NTL::conv<unsigned long>(v))  
                        + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};


template <unsigned long int N>
void
subsetprod_mod(const std::vector<long> &set, const std::array<NTL::ZZ, N> &bases,
        const std::function<int(std::array<NTL::ZZ, N>&, const std::vector<size_t>&, size_t)> &callback,
        size_t min_terms=2, size_t max_terms=0)
{
    constexpr size_t num_bases { N };

    const size_t set_size { set.size() };
    min_terms = MAX(min_terms, 1);
    max_terms = max_terms > 0 ? MIN(set_size, max_terms) : set_size;

    std::array<NTL::ZZ, num_bases> default_prod;
    std::fill(default_prod.begin(), default_prod.end(), 1);

    // go through all possible subset sizes starting from 2
    const size_t max_num_subsets {calc_max_subsets(set_size, min_terms, max_terms)};

#if LOG_LEVEL >= 1
    std::cout << "> enumerating " << max_num_subsets << " subsets [" << min_terms
        << "," << max_terms << "] ...\n";
#endif

    size_t subset_count {0};
    for (size_t factors {min_terms}; factors <= max_terms; factors++) 
    {
        std::vector<size_t> index_stack {0};
        index_stack.reserve(factors);

        /* cache of products */
        std::vector<std::array<NTL::ZZ, num_bases> > products; 
        products.reserve(factors);

        // go through subsets of size factors suing a 'stack' (or odometer?)
        size_t index_size;
        while ((index_size = index_stack.size()) > 0) 
        {
            const size_t prod_size  = products.size();
            size_t top = index_size - 1;
            size_t prod_top = prod_size - 1;

            if (index_stack[top] < set_size - (factors - index_size))
            {
                NTL::ZZ p_zz { set[index_stack[top]] };
                if (prod_size+1 < factors) 
                {
                    std::array<NTL::ZZ, num_bases> prods;
                    for(size_t j { 0 }; j < num_bases; ++j)
                    {
                        NTL::ZZ prod { 1 };
                        if (prod_size)
                            prod = products[prod_top][j];
                        NTL::MulMod(prods[j], prod, p_zz, bases[j]);
                    }
                    products.push_back(std::move(prods));
                }

                if (index_size < factors) 
                {
                    index_stack.push_back(index_stack[top]+1);
                    continue;
                }

                const std::array<NTL::ZZ, num_bases> &prods {
                    prod_size > 0 ? products[prod_top] : default_prod };

                /* multiply the current number p_zz to get the product
                 * of the current subset*/
                std::array<NTL::ZZ, num_bases> mod_prod;
                for(size_t j {0}; j < num_bases; ++j)
                    NTL::MulMod(mod_prod[j], prods[j], p_zz, bases[j]);

                callback(mod_prod, index_stack, subset_count);
                index_stack[top]++;
            }
            else
            {
                index_stack.pop_back();
                if (top > 0) {
                    ++index_stack[top - 1];
                    products.pop_back();
                }

                continue;
            }
        }
    }
}


template <size_t N>
void subsetprod_2way_all(
    std::vector<std::vector<long>> &solns,
    const std::vector<long> &set,
    const std::array<NTL::ZZ, N>  &targets,
    const std::array<NTL::ZZ, N>  &bases,
    size_t min_size, size_t max_size)
{
    std::unordered_map<std::array<NTL::ZZ, N>, std::vector<std::vector<bool>>, ArrayHasher<NTL::ZZ, N>> map;

    /* split primes into two vectors */
    std::vector<long> first_half, second_half;
    split_half<long>(first_half, second_half, set);
    const size_t first_half_size {first_half.size()};

    /* calculate group size */
    const NTL::ZZ lcm {get_lcm<std::array<NTL::ZZ, N> >(bases, N)};
    const NTL::ZZ mod_G {eulers_toitent(lcm)};

    /* clip min_size and max_size */
    bound<size_t>(min_size, 1, first_half_size);
    if (max_size)
        bound<size_t>(max_size, min_size, first_half_size);
    else
        max_size = first_half_size;

    /* reserve space in hashmap */
    size_t num_subsets {calc_max_subsets(first_half_size, min_size, max_size)};
    constexpr size_t max_size_t {(size_t)-1};
    const size_t map_capacity {mod_G > max_size_t ? num_subsets 
        : MIN(num_subsets, NTL::conv<size_t>(mod_G)) };

#if LOG_LEVEL >= 1
    std::cout << "|G| = " << mod_G << " (" << ceil(NTL::log(mod_G)/log(2)) << " bits)\n";
    std::cout << "subset sizes " << "[" << min_size << "," << max_size << "]: "
        << num_subsets << " subsets\n";
    std::cout << "map capacity: " << map_capacity << "\n";
    std::cout << "storing inverse subset products of first half (size "
        << first_half_size << ") " << " ...\n";
    size_t inv_count {0};
#if LOG_LEVEL >= 2
    size_t count {0};
#endif

    map.reserve(map_capacity);

#endif

    /* go through every subset in first half, store inverse in hashmap */
    subsetprod_mod<N>(first_half, bases, 
            [&](std::array<NTL::ZZ, N>& prod_cache,
                const std::vector<size_t> &indicies, size_t insert_index)->int
            {
                std::array<NTL::ZZ, N> invs;
                for(size_t i{0}; i < N; ++i)
                {
                    if (!NTL::IsOne(targets[i]))
                        NTL::MulMod(prod_cache[i], prod_cache[i], targets[i], bases[i]);
                   invs[i] = NTL::InvMod(prod_cache[i], bases[i]);
                }

                std::vector<std::vector<bool>> &vec {map[invs]};

                // create vector<bool> representing this subset
                std::vector<bool> subset;
                subset.reserve(first_half_size);
                subset.resize(first_half_size, false);
                for (auto i : indicies) subset[i] = true;

                vec.push_back(std::move(subset));
#if LOG_LEVEL >= 1
                if(vec.size() == 1)
                    inv_count++;
#if LOG_LEVEL >= 2
                if((count++ & STEP_MASK) == 0)
                    std::cerr << "count: " << count << "\r";
#endif
#endif
                return 1;
            }, min_size, max_size);

#if LOG_LEVEL >= 1
#if LOG_LEVEL >= 2
    std::cerr << "count: " << count << "\r";
    count = 0;
#endif
    std::cout << "found " << inv_count << " distinct subset products\n";
    std::cout << "checking for inverse subset products of second half (size "
        << second_half.size() << ") ...\n";
#endif

    subsetprod_mod<N>(second_half, bases,
            [&](std::array<NTL::ZZ, N>& prod_cache, 
                const std::vector<size_t> &indicies, size_t insert_index)->int
            {
                auto it { map.find(prod_cache)  };
                if (it != map.end())
                {
                    for (const std::vector<bool> &other_half : it->second)
                    {
                        std::vector<long> soln;
                        soln.reserve(indicies.size() + other_half.size());
                        for(size_t i {0}; i < other_half.size(); i++) 
                            if (other_half[i])
                                soln.push_back(first_half[i]);

                        for(auto i : indicies)
                            soln.push_back(second_half[i]);
                        std::cout << "found: " << soln << "\n";
                        solns.push_back(std::move(soln));
                    }
                }
#if LOG_LEVEL >= 2
                if((count++ & STEP_MASK) == 0)
                    std::cerr << "count: " << count << "\r";
#endif
                return 0;
            }, min_size, max_size, false);
}

#endif

