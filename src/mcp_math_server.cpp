// ============================================================================
//  mcp_math_server.cpp — MCP STDIO Server: Math & Statistics Tools
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that provides math and statistics tools.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_math_server.cpp -o bin/mcp_math_server
// ============================================================================
#include "poorimcp.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
#include <functional>
#include <unordered_map>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    constexpr int MAX_RANDOM_COUNT = 100'000;
    constexpr std::size_t MAX_STATS_NUMBERS = 1'000'000;

    struct NumberExtraction
    {
        std::optional<double> value;
        bool was_present;
    };

    NumberExtraction get_number(const JSON& args, const std::string& key, double fallback)
    {
        NumberExtraction result;
        if (!args.contains(key))
        {
            result.was_present = false;
            result.value = fallback;
        }
        else
        {
            result.was_present = true;
            if (!args[key].is_number())
            {
                result.value = std::nullopt;
            }
            else
            {
                result.value = args[key].get_number();
            }
        }
        return result;
    }

    std::optional<std::vector<double>> parse_number_array(const JSON& args, const std::string& key)
    {
        std::optional<std::vector<double>> result{std::nullopt};
        if (args.contains(key) && args[key].is_array())
        {
            std::vector<double> values{};
            bool valid{true};
            for (const auto& val : args[key])
            {
                if (val.is_number())
                {
                    values.push_back(val.get_number());
                }
                else
                {
                    valid = false;
                    break;
                }
            }
            if (valid)
            {
                result = values;
            }
        }
        return result;
    }

    struct CountExtraction
    {
        int count;
        bool error;
        std::string error_msg;
    };

    CountExtraction get_random_count(const JSON& args)
    {
        CountExtraction result;
        result.count = 1;
        result.error = false;
        if (args.contains("count"))
        {
            if (!args["count"].is_number())
            {
                result.error = true;
                result.error_msg = "'count' must be an integer.";
            }
            else
            {
                result.count = static_cast<int>(args["count"].get_number());
                if (result.count < 1)
                {
                    result.count = 1;
                }
                if (result.count > MAX_RANDOM_COUNT)
                {
                    result.error = true;
                    result.error_msg = "'count' exceeds the maximum of " + std::to_string(MAX_RANDOM_COUNT) + ".";
                }
            }
        }
        return result;
    }

    // --- Tool Implementations ---

    AsyncTask<JSON> tool_random_uniform_int(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto min_res = get_number(args, "min_val", 0.0);
            auto max_res = get_number(args, "max_val", 100.0);
            if (!min_res.was_present || !max_res.was_present)
            {
                result["error"] = "Missing required parameters 'min_val' or 'max_val'.";
                result["is_error"] = true;
            }
            else if (!min_res.value || !max_res.value)
            {
                result["error"] = "'min_val' and 'max_val' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                long long a = static_cast<long long>(*min_res.value);
                long long b = static_cast<long long>(*max_res.value);
                if (a > b)
                {
                    std::swap(a, b);
                }
                thread_local std::mt19937_64 gen{std::random_device{}()};
                std::uniform_int_distribution<long long> dis(a, b);
                JSONArray numbers{};
                for (int i = 0; i < count_res.count; ++i)
                {
                    numbers.push_back(static_cast<double>(dis(gen)));
                }
                if (count_res.count == 1)
                {
                    result["result"] = numbers[0];
                    result["is_error"] = false;
                }
                else
                {
                    result["result"] = std::move(numbers);
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_uniform_real(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto min_res = get_number(args, "min_val", 0.0);
            auto max_res = get_number(args, "max_val", 1.0);
            if (!min_res.was_present || !max_res.was_present)
            {
                result["error"] = "Missing required parameters 'min_val' or 'max_val'.";
                result["is_error"] = true;
            }
            else if (!min_res.value || !max_res.value)
            {
                result["error"] = "'min_val' and 'max_val' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double a = *min_res.value;
                double b = *max_res.value;
                if (a > b)
                {
                    std::swap(a, b);
                }
                thread_local std::mt19937_64 gen{std::random_device{}()};
                std::uniform_real_distribution<double> dis(a, b);
                JSONArray numbers{};
                for (int i = 0; i < count_res.count; ++i)
                {
                    numbers.push_back(dis(gen));
                }
                if (count_res.count == 1)
                {
                    result["result"] = numbers[0];
                    result["is_error"] = false;
                }
                else
                {
                    result["result"] = std::move(numbers);
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_bernoulli(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto p_res = get_number(args, "p", 0.5);
            if (!p_res.was_present)
            {
                result["error"] = "Missing required parameter 'p'.";
                result["is_error"] = true;
            }
            else if (!p_res.value)
            {
                result["error"] = "'p' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double p = *p_res.value;
                if (p < 0.0 || p > 1.0)
                {
                    result["error"] = "Probability 'p' must be between 0 and 1.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::bernoulli_distribution dis(p);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen) ? 1.0 : 0.0);
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_binomial(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto t_res = get_number(args, "t", 1.0);
            auto p_res = get_number(args, "p", 0.5);
            if (!t_res.was_present || !p_res.was_present)
            {
                result["error"] = "Missing required parameters 't' or 'p'.";
                result["is_error"] = true;
            }
            else if (!t_res.value || !p_res.value)
            {
                result["error"] = "'t' and 'p' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                int t = static_cast<int>(*t_res.value);
                double p = *p_res.value;
                if (p < 0.0 || p > 1.0)
                {
                    result["error"] = "Probability 'p' must be between 0 and 1.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::binomial_distribution<int> dis(t, p);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(static_cast<double>(dis(gen)));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_geometric(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto p_res = get_number(args, "p", 0.5);
            if (!p_res.was_present)
            {
                result["error"] = "Missing required parameter 'p'.";
                result["is_error"] = true;
            }
            else if (!p_res.value)
            {
                result["error"] = "'p' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double p = *p_res.value;
                if (p < 0.0 || p > 1.0)
                {
                    result["error"] = "Probability 'p' must be between 0 and 1.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::geometric_distribution<int> dis(p);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(static_cast<double>(dis(gen)));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_negative_binomial(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto k_res = get_number(args, "k", 1.0);
            auto p_res = get_number(args, "p", 0.5);
            if (!k_res.was_present || !p_res.was_present)
            {
                result["error"] = "Missing required parameters 'k' or 'p'.";
                result["is_error"] = true;
            }
            else if (!k_res.value || !p_res.value)
            {
                result["error"] = "'k' and 'p' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                int k = static_cast<int>(*k_res.value);
                double p = *p_res.value;
                if (p < 0.0 || p > 1.0)
                {
                    result["error"] = "Probability 'p' must be between 0 and 1.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::negative_binomial_distribution<int> dis(k, p);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(static_cast<double>(dis(gen)));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_poisson(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto mean_res = get_number(args, "mean", 1.0);
            if (!mean_res.was_present)
            {
                result["error"] = "Missing required parameter 'mean'.";
                result["is_error"] = true;
            }
            else if (!mean_res.value)
            {
                result["error"] = "'mean' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double mean = *mean_res.value;
                if (mean < 0.0)
                {
                    result["error"] = "'mean' must be non-negative.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::poisson_distribution<int> dis(mean);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(static_cast<double>(dis(gen)));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_exponential(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto lambda_res = get_number(args, "lambda", 1.0);
            if (!lambda_res.was_present)
            {
                result["error"] = "Missing required parameter 'lambda'.";
                result["is_error"] = true;
            }
            else if (!lambda_res.value)
            {
                result["error"] = "'lambda' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double lambda = *lambda_res.value;
                if (lambda <= 0.0)
                {
                    result["error"] = "'lambda' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::exponential_distribution<double> dis(lambda);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_gamma(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto alpha_res = get_number(args, "alpha", 1.0);
            auto beta_res = get_number(args, "beta", 1.0);
            if (!alpha_res.was_present || !beta_res.was_present)
            {
                result["error"] = "Missing required parameters 'alpha' or 'beta'.";
                result["is_error"] = true;
            }
            else if (!alpha_res.value || !beta_res.value)
            {
                result["error"] = "'alpha' and 'beta' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double alpha = *alpha_res.value;
                double beta = *beta_res.value;
                if (alpha <= 0.0 || beta <= 0.0)
                {
                    result["error"] = "'alpha' and 'beta' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::gamma_distribution<double> dis(alpha, beta);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_weibull(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto shape_res = get_number(args, "shape", 1.0);
            auto scale_res = get_number(args, "scale", 1.0);
            if (!shape_res.was_present || !scale_res.was_present)
            {
                result["error"] = "Missing required parameters 'shape' or 'scale'.";
                result["is_error"] = true;
            }
            else if (!shape_res.value || !scale_res.value)
            {
                result["error"] = "'shape' and 'scale' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double shape = *shape_res.value;
                double scale = *scale_res.value;
                if (shape <= 0.0 || scale <= 0.0)
                {
                    result["error"] = "'shape' and 'scale' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::weibull_distribution<double> dis(shape, scale);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_extreme_value(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto loc_res = get_number(args, "location", 0.0);
            auto scale_res = get_number(args, "scale", 1.0);
            if (!loc_res.was_present || !scale_res.was_present)
            {
                result["error"] = "Missing required parameters 'location' or 'scale'.";
                result["is_error"] = true;
            }
            else if (!loc_res.value || !scale_res.value)
            {
                result["error"] = "'location' and 'scale' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double loc = *loc_res.value;
                double scale = *scale_res.value;
                if (scale <= 0.0)
                {
                    result["error"] = "'scale' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::extreme_value_distribution<double> dis(loc, scale);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_normal(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto mean_res = get_number(args, "mean", 0.0);
            auto stddev_res = get_number(args, "stddev", 1.0);
            if (!mean_res.was_present || !stddev_res.was_present)
            {
                result["error"] = "Missing required parameters 'mean' or 'stddev'.";
                result["is_error"] = true;
            }
            else if (!mean_res.value || !stddev_res.value)
            {
                result["error"] = "'mean' and 'stddev' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double mean = *mean_res.value;
                double stddev = *stddev_res.value;
                if (stddev < 0.0)
                {
                    result["error"] = "'stddev' must be >= 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::normal_distribution<double> dis(mean, stddev);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_lognormal(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto mean_res = get_number(args, "mean", 0.0);
            auto stddev_res = get_number(args, "stddev", 1.0);
            if (!mean_res.was_present || !stddev_res.was_present)
            {
                result["error"] = "Missing required parameters 'mean' or 'stddev'.";
                result["is_error"] = true;
            }
            else if (!mean_res.value || !stddev_res.value)
            {
                result["error"] = "'mean' and 'stddev' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double mean = *mean_res.value;
                double stddev = *stddev_res.value;
                if (stddev < 0.0)
                {
                    result["error"] = "'stddev' must be >= 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::lognormal_distribution<double> dis(mean, stddev);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_chi_squared(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto n_res = get_number(args, "n", 1.0);
            if (!n_res.was_present)
            {
                result["error"] = "Missing required parameter 'n'.";
                result["is_error"] = true;
            }
            else if (!n_res.value)
            {
                result["error"] = "'n' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double n = *n_res.value;
                if (n <= 0.0)
                {
                    result["error"] = "Degrees of freedom 'n' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::chi_squared_distribution<double> dis(n);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_cauchy(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto loc_res = get_number(args, "location", 0.0);
            auto scale_res = get_number(args, "scale", 1.0);
            if (!loc_res.was_present || !scale_res.was_present)
            {
                result["error"] = "Missing required parameters 'location' or 'scale'.";
                result["is_error"] = true;
            }
            else if (!loc_res.value || !scale_res.value)
            {
                result["error"] = "'location' and 'scale' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double loc = *loc_res.value;
                double scale = *scale_res.value;
                if (scale <= 0.0)
                {
                    result["error"] = "'scale' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::cauchy_distribution<double> dis(loc, scale);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_fisher_f(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto m_res = get_number(args, "m", 1.0);
            auto n_res = get_number(args, "n", 1.0);
            if (!m_res.was_present || !n_res.was_present)
            {
                result["error"] = "Missing required parameters 'm' or 'n'.";
                result["is_error"] = true;
            }
            else if (!m_res.value || !n_res.value)
            {
                result["error"] = "'m' and 'n' must be numbers.";
                result["is_error"] = true;
            }
            else
            {
                double m = *m_res.value;
                double n = *n_res.value;
                if (m <= 0.0 || n <= 0.0)
                {
                    result["error"] = "Degrees of freedom 'm' and 'n' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::fisher_f_distribution<double> dis(m, n);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_random_student_t(const JSON& args)
    {
        JSON result;
        auto count_res = get_random_count(args);
        if (count_res.error)
        {
            result["error"] = count_res.error_msg;
            result["is_error"] = true;
        }
        else
        {
            auto n_res = get_number(args, "n", 1.0);
            if (!n_res.was_present)
            {
                result["error"] = "Missing required parameter 'n'.";
                result["is_error"] = true;
            }
            else if (!n_res.value)
            {
                result["error"] = "'n' must be a number.";
                result["is_error"] = true;
            }
            else
            {
                double n = *n_res.value;
                if (n <= 0.0)
                {
                    result["error"] = "Degrees of freedom 'n' must be greater than 0.";
                    result["is_error"] = true;
                }
                else
                {
                    thread_local std::mt19937_64 gen{std::random_device{}()};
                    std::student_t_distribution<double> dis(n);
                    JSONArray numbers{};
                    for (int i = 0; i < count_res.count; ++i)
                    {
                        numbers.push_back(dis(gen));
                    }
                    if (count_res.count == 1)
                    {
                        result["result"] = numbers[0];
                        result["is_error"] = false;
                    }
                    else
                    {
                        result["result"] = std::move(numbers);
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_calculate_statistics(const JSON& args)
    {
        JSON result;
        if (!args.contains("numbers") || !args["numbers"].is_array())
        {
            result["error"] = "Missing or invalid 'numbers' array in arguments";
            result["is_error"] = true;
        }
        else if (args["numbers"].size() > MAX_STATS_NUMBERS)
        {
            result["error"] = "'numbers' array exceeds the maximum of " + std::to_string(MAX_STATS_NUMBERS) + " elements.";
            result["is_error"] = true;
        }
        else
        {
            std::vector<double> nums;
            nums.reserve(args["numbers"].size());
            for (const auto& val : args["numbers"])
            {
                if (val.is_number())
                {
                    nums.push_back(val.get_number());
                }
            }
            if (nums.empty())
            {
                result["error"] = "The numbers array is empty (or contained no numeric values).";
                result["is_error"] = true;
            }
            else
            {
                std::size_t n = nums.size();
                double sum = std::accumulate(nums.begin(), nums.end(), 0.0);
                double mean = sum / static_cast<double>(n);

                auto [min_it, max_it] = std::minmax_element(nums.begin(), nums.end());
                double min_val = *min_it;
                double max_val = *max_it;

                std::vector<double> sorted_nums = nums;
                std::sort(sorted_nums.begin(), sorted_nums.end());
                double median = (n % 2 == 0) ? (sorted_nums[n / 2 - 1] + sorted_nums[n / 2]) / 2.0 : sorted_nums[n / 2];

                double sq_sum = 0.0;
                for (double x : nums)
                {
                    sq_sum += (x - mean) * (x - mean);
                }
                double variance = sq_sum / static_cast<double>(n);
                double std_dev = std::sqrt(variance);

                double product = 1.0;
                double sum_inv = 0.0;
                bool geo_valid = true;
                bool har_valid = true;
                for (double x : nums)
                {
                    if (x <= 0.0) { geo_valid = false; }
                    if (x == 0.0) { har_valid = false; }
                    product *= x;
                    sum_inv += 1.0 / x;
                }

                JSON stats;
                stats["count"] = static_cast<double>(n);
                stats["sum"] = sum;
                stats["mean"] = mean;
                stats["median"] = median;
                stats["min"] = min_val;
                stats["max"] = max_val;
                stats["variance"] = variance;
                stats["standard_deviation"] = std_dev;

                if (geo_valid)
                {
                    stats["geometric_mean"] = std::pow(product, 1.0 / static_cast<double>(n));
                }
                if (har_valid)
                {
                    stats["harmonic_mean"] = static_cast<double>(n) / sum_inv;
                }

                double sq_total = 0.0;
                for (double x : nums)
                {
                    sq_total += x * x;
                }
                stats["quadratic_mean"] = std::sqrt(sq_total / static_cast<double>(n));

                result["result"] = std::move(stats);
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> tool_arithmetic_operation(const JSON& args)
    {
        JSON result;
        if (!args.contains("operation") || !args.contains("a") || !args.contains("b"))
        {
            result["error"] = "Missing 'operation', 'a', or 'b' in arguments";
            result["is_error"] = true;
        }
        else if (!args["operation"].is_string() || !args["a"].is_number() || !args["b"].is_number())
        {
            result["error"] = "'operation' must be a string, and 'a'/'b' must be numbers.";
            result["is_error"] = true;
        }
        else
        {
            std::string op = args["operation"].get_string();
            double a = args["a"].get_number();
            double b = args["b"].get_number();
            double result_value = 0.0;
            bool error_state = false;

            if (op == "add") { result_value = a + b; }
            else if (op == "subtract") { result_value = a - b; }
            else if (op == "multiply") { result_value = a * b; }
            else if (op == "divide")
            {
                if (b == 0.0)
                {
                    result["error"] = "Division by zero";
                    result["is_error"] = true;
                    error_state = true;
                }
                else { result_value = a / b; }
            }
            else if (op == "modulo")
            {
                if (b == 0.0)
                {
                    result["error"] = "Modulo by zero";
                    result["is_error"] = true;
                    error_state = true;
                }
                else { result_value = std::fmod(a, b); }
            }
            else if (op == "power") { result_value = std::pow(a, b); }
            else
            {
                result["error"] = "Unknown operation: " + op;
                result["is_error"] = true;
                error_state = true;
            }

            if (!error_state)
            {
                if (!std::isfinite(result_value))
                {
                    result["error"] = "Result is not a finite number (overflow, NaN, or infinity).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = result_value;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> arithmetic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            auto mean = std::reduce(values_opt->cbegin(), values_opt->cend(), 0.0, std::plus<double>()) / static_cast<double>(values_opt->size());
            result["result"] = mean;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> geometric_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double product{1.0};
            bool valid{true};
            for (double v : *values_opt)
            {
                if (v < 0.0) { valid = false; break; }
                product *= v;
            }
            if (!valid)
            {
                result["error"] = "Geometric mean requires non-negative numbers.";
                result["is_error"] = true;
            }
            else
            {
                double mean = std::pow(product, 1.0 / static_cast<double>(values_opt->size()));
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> harmonic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double sum_inv{0.0};
            bool valid{true};
            for (double v : *values_opt)
            {
                if (v == 0.0) { valid = false; break; }
                sum_inv += 1.0 / v;
            }
            if (!valid)
            {
                result["error"] = "Harmonic mean requires non-zero numbers.";
                result["is_error"] = true;
            }
            else
            {
                double mean = static_cast<double>(values_opt->size()) / sum_inv;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> generalized_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto p_res = get_number(args, "p", 1.0);

        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (!p_res.value)
        {
            result["error"] = "Parameter 'p' must be a number.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double p = *p_res.value;
            double sum_p{0.0};
            bool valid{true};

            if (p == 0.0)
            {
                double product{1.0};
                for (double v : *values_opt)
                {
                    if (v <= 0.0) { valid = false; break; }
                    product *= v;
                }
                if (valid)
                {
                    double mean = std::pow(product, 1.0 / static_cast<double>(values_opt->size()));
                    result["result"] = mean;
                    result["is_error"] = false;
                }
                else
                {
                    result["error"] = "Generalized mean with p=0 (geometric) requires positive numbers.";
                    result["is_error"] = true;
                }
            }
            else
            {
                for (double v : *values_opt)
                {
                    if (v < 0.0 && std::floor(p) != p) { valid = false; break; }
                    sum_p += std::pow(v, p);
                }
                if (!valid)
                {
                    result["error"] = "Generalized mean requires non-negative numbers for fractional p.";
                    result["is_error"] = true;
                }
                else
                {
                    double mean = std::pow(sum_p / static_cast<double>(values_opt->size()), 1.0 / p);
                    if (!std::isfinite(mean))
                    {
                        result["error"] = "Result is not finite (overflow).";
                        result["is_error"] = true;
                    }
                    else
                    {
                        result["result"] = mean;
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> weighted_generalized_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto weights_opt = parse_number_array(args, "weights");
        auto p_res = get_number(args, "p", 1.0);

        if (!values_opt || !weights_opt)
        {
            result["error"] = "Missing or invalid 'values' or 'weights' array.";
            result["is_error"] = true;
        }
        else if (!p_res.value)
        {
            result["error"] = "Parameter 'p' must be a number.";
            result["is_error"] = true;
        }
        else if (values_opt->empty() || weights_opt->empty())
        {
            result["error"] = "'values' and 'weights' arrays cannot be empty.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != weights_opt->size())
        {
            result["error"] = "'values' and 'weights' arrays must have the same length.";
            result["is_error"] = true;
        }
        else
        {
            double p = *p_res.value;
            double sum_wp{0.0};
            double sum_w{0.0};
            bool valid{true};

            if (p == 0.0)
            {
                double sum_wlogv{0.0};
                for (std::size_t i = 0; i < values_opt->size(); ++i)
                {
                    double v = (*values_opt)[i];
                    double w = (*weights_opt)[i];
                    if (v <= 0.0 || w < 0.0) { valid = false; break; }
                    sum_w += w;
                    sum_wlogv += w * std::log(v);
                }
                if (valid)
                {
                    double mean = std::exp(sum_wlogv / sum_w);
                    result["result"] = mean;
                    result["is_error"] = false;
                }
                else
                {
                    result["error"] = "Weighted generalized mean with p=0 requires positive values and non-negative weights.";
                    result["is_error"] = true;
                }
            }
            else
            {
                for (std::size_t i = 0; i < values_opt->size(); ++i)
                {
                    double v = (*values_opt)[i];
                    double w = (*weights_opt)[i];
                    if (v < 0.0 && std::floor(p) != p) { valid = false; break; }
                    if (w < 0.0) { valid = false; break; }
                    sum_w += w;
                    sum_wp += w * std::pow(v, p);
                }
                if (!valid)
                {
                    result["error"] = "Weighted generalized mean requires non-negative weights and valid values for p.";
                    result["is_error"] = true;
                }
                else if (sum_w == 0.0)
                {
                    result["error"] = "Sum of weights is zero.";
                    result["is_error"] = true;
                }
                else
                {
                    double mean = std::pow(sum_wp / sum_w, 1.0 / p);
                    if (!std::isfinite(mean))
                    {
                        result["error"] = "Result is not finite (overflow).";
                        result["is_error"] = true;
                    }
                    else
                    {
                        result["result"] = mean;
                        result["is_error"] = false;
                    }
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> lehmer_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto p_res = get_number(args, "p", 1.0);

        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (!p_res.value)
        {
            result["error"] = "Parameter 'p' must be a number.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double p = *p_res.value;
            double sum_v_p{0.0};
            double sum_v_p1{0.0};

            for (double v : *values_opt)
            {
                sum_v_p += std::pow(v, p);
                sum_v_p1 += std::pow(v, p + 1.0);
            }
            if (sum_v_p == 0.0)
            {
                result["error"] = "Division by zero in Lehmer mean (sum of v^p is 0).";
                result["is_error"] = true;
            }
            else
            {
                double mean = sum_v_p1 / sum_v_p;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> weighted_lehmer_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto weights_opt = parse_number_array(args, "weights");
        auto p_res = get_number(args, "p", 1.0);

        if (!values_opt || !weights_opt)
        {
            result["error"] = "Missing or invalid 'values' or 'weights' array.";
            result["is_error"] = true;
        }
        else if (!p_res.value)
        {
            result["error"] = "Parameter 'p' must be a number.";
            result["is_error"] = true;
        }
        else if (values_opt->empty() || weights_opt->empty())
        {
            result["error"] = "'values' and 'weights' arrays cannot be empty.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != weights_opt->size())
        {
            result["error"] = "'values' and 'weights' arrays must have the same length.";
            result["is_error"] = true;
        }
        else
        {
            double p = *p_res.value;
            double sum_wv_p{0.0};
            double sum_wv_p1{0.0};
            bool valid{true};

            for (std::size_t i = 0; i < values_opt->size(); ++i)
            {
                double v = (*values_opt)[i];
                double w = (*weights_opt)[i];
                if (w < 0.0) { valid = false; break; }
                sum_wv_p += w * std::pow(v, p);
                sum_wv_p1 += w * std::pow(v, p + 1.0);
            }
            if (!valid)
            {
                result["error"] = "Weights must be non-negative.";
                result["is_error"] = true;
            }
            else if (sum_wv_p == 0.0)
            {
                result["error"] = "Division by zero in weighted Lehmer mean.";
                result["is_error"] = true;
            }
            else
            {
                double mean = sum_wv_p1 / sum_wv_p;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> contraharmonic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double sum_v{0.0};
            double sum_v2{0.0};
            for (double v : *values_opt)
            {
                sum_v += v;
                sum_v2 += v * v;
            }
            if (sum_v == 0.0)
            {
                result["error"] = "Division by zero in contraharmonic mean.";
                result["is_error"] = true;
            }
            else
            {
                double mean = sum_v2 / sum_v;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> quadratic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double sum_sq{0.0};
            for (double v : *values_opt)
            {
                sum_sq += v * v;
            }
            double mean = std::sqrt(sum_sq / static_cast<double>(values_opt->size()));
            if (!std::isfinite(mean))
            {
                result["error"] = "Result is not finite (overflow).";
                result["is_error"] = true;
            }
            else
            {
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> cubic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double sum_cb{0.0};
            for (double v : *values_opt)
            {
                sum_cb += v * v * v;
            }
            double mean = std::cbrt(sum_cb / static_cast<double>(values_opt->size()));
            if (!std::isfinite(mean))
            {
                result["error"] = "Result is not finite (overflow).";
                result["is_error"] = true;
            }
            else
            {
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> midrange_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            auto [min_it, max_it] = std::minmax_element(values_opt->begin(), values_opt->end());
            double mean = (*min_it + *max_it) / 2.0;
            result["result"] = mean;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> trimean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() < 4)
        {
            result["error"] = "Trimean requires at least 4 values.";
            result["is_error"] = true;
        }
        else
        {
            std::vector<double> sorted_vals = *values_opt;
            std::sort(sorted_vals.begin(), sorted_vals.end());
            std::size_t n = sorted_vals.size();

            auto quantile = [&](double q) -> double
            {
                double res{0.0};
                double pos = q * (n - 1);
                std::size_t lower = static_cast<std::size_t>(std::floor(pos));
                std::size_t upper = static_cast<std::size_t>(std::ceil(pos));
                double frac = pos - lower;
                if (lower == upper) { res = sorted_vals[lower]; }
                else { res = sorted_vals[lower] * (1.0 - frac) + sorted_vals[upper] * frac; }
                return res;
            };

            double q1 = quantile(0.25);
            double median = quantile(0.50);
            double q3 = quantile(0.75);
            double mean = (q1 + 2.0 * median + q3) / 4.0;
            result["result"] = mean;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> interquartile_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            std::vector<double> sorted_vals = *values_opt;
            std::sort(sorted_vals.begin(), sorted_vals.end());
            std::size_t n = sorted_vals.size();

            std::size_t trim_count = n / 4;
            if (n - 2 * trim_count == 0)
            {
                result["error"] = "Not enough values to calculate interquartile mean.";
                result["is_error"] = true;
            }
            else
            {
                double sum_iqm = std::accumulate(sorted_vals.begin() + trim_count, sorted_vals.end() - trim_count, 0.0);
                double mean = sum_iqm / static_cast<double>(n - 2 * trim_count);
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> trimmed_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto trim_res = get_number(args, "trim_percent", 5.0);

        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (!trim_res.value)
        {
            result["error"] = "'trim_percent' must be a number.";
            result["is_error"] = true;
        }
        else if (*trim_res.value < 0.0 || *trim_res.value >= 50.0)
        {
            result["error"] = "'trim_percent' must be between 0 and 50 (exclusive).";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            std::vector<double> sorted_vals = *values_opt;
            std::sort(sorted_vals.begin(), sorted_vals.end());
            std::size_t n = sorted_vals.size();

            double trim_percent = *trim_res.value;
            std::size_t trim_count = static_cast<std::size_t>(std::floor((trim_percent / 100.0) * n));

            if (n - 2 * trim_count == 0)
            {
                result["error"] = "Trim count exceeds array size. Reduce trim_percent.";
                result["is_error"] = true;
            }
            else
            {
                double sum = std::accumulate(sorted_vals.begin() + trim_count, sorted_vals.end() - trim_count, 0.0);
                double mean = sum / static_cast<double>(n - 2 * trim_count);
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> winsorized_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto winsor_res = get_number(args, "winsor_percent", 5.0);

        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (!winsor_res.value)
        {
            result["error"] = "'winsor_percent' must be a number.";
            result["is_error"] = true;
        }
        else if (*winsor_res.value < 0.0 || *winsor_res.value >= 50.0)
        {
            result["error"] = "'winsor_percent' must be between 0 and 50 (exclusive).";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            std::vector<double> sorted_vals = *values_opt;
            std::sort(sorted_vals.begin(), sorted_vals.end());
            std::size_t n = sorted_vals.size();

            double winsor_percent = *winsor_res.value;
            std::size_t winsor_count = static_cast<std::size_t>(std::floor((winsor_percent / 100.0) * n));

            if (n - 2 * winsor_count == 0 && n != 1)
            {
                result["error"] = "Winsor count exceeds array size. Reduce winsor_percent.";
                result["is_error"] = true;
            }
            else
            {
                std::vector<double> winsorized_vals = sorted_vals;
                if (winsor_count > 0)
                {
                    double lower_bound = sorted_vals[winsor_count - 1];
                    double upper_bound = sorted_vals[n - winsor_count];
                    for (std::size_t i = 0; i < winsor_count; ++i) { winsorized_vals[i] = lower_bound; }
                    for (std::size_t i = n - winsor_count; i < n; ++i) { winsorized_vals[i] = upper_bound; }
                }
                double sum = std::accumulate(winsorized_vals.begin(), winsorized_vals.end(), 0.0);
                double mean = sum / static_cast<double>(n);
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> logarithmic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != 2)
        {
            result["error"] = "Logarithmic mean is strictly defined for exactly 2 numbers.";
            result["is_error"] = true;
        }
        else
        {
            double x = (*values_opt)[0];
            double y = (*values_opt)[1];
            if (x <= 0.0 || y <= 0.0)
            {
                result["error"] = "Logarithmic mean requires strictly positive numbers.";
                result["is_error"] = true;
            }
            else if (x == y)
            {
                result["result"] = x;
                result["is_error"] = false;
            }
            else
            {
                double mean = (y - x) / (std::log(y) - std::log(x));
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite.";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> arithmetic_geometric_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != 2)
        {
            result["error"] = "Arithmetic-Geometric mean is strictly defined for exactly 2 numbers.";
            result["is_error"] = true;
        }
        else
        {
            double a = (*values_opt)[0];
            double b = (*values_opt)[1];
            if (a < 0.0 || b < 0.0)
            {
                result["error"] = "Arithmetic-Geometric mean requires non-negative numbers.";
                result["is_error"] = true;
            }
            else
            {
                constexpr int MAX_ITERATIONS = 1000;
                constexpr double TOLERANCE = 1e-15;
                int iterations{0};

                while (iterations < MAX_ITERATIONS)
                {
                    double next_a = (a + b) / 2.0;
                    double next_b = std::sqrt(a * b);
                    if (std::abs(next_a - next_b) < TOLERANCE)
                    {
                        a = next_a;
                        b = next_b;
                        break;
                    }
                    a = next_a;
                    b = next_b;
                    iterations++;
                }
                double mean = (a + b) / 2.0;
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> geometric_harmonic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != 2)
        {
            result["error"] = "Geometric-Harmonic mean is strictly defined for exactly 2 numbers.";
            result["is_error"] = true;
        }
        else
        {
            double a = (*values_opt)[0];
            double b = (*values_opt)[1];
            if (a <= 0.0 || b <= 0.0)
            {
                result["error"] = "Geometric-Harmonic mean requires strictly positive numbers.";
                result["is_error"] = true;
            }
            else
            {
                constexpr int MAX_ITERATIONS = 1000;
                constexpr double TOLERANCE = 1e-15;
                int iterations{0};

                while (iterations < MAX_ITERATIONS)
                {
                    double next_a = std::sqrt(a * b);
                    double next_b = 2.0 * a * b / (a + b);
                    if (std::abs(next_a - next_b) < TOLERANCE)
                    {
                        a = next_a;
                        b = next_b;
                        break;
                    }
                    a = next_a;
                    b = next_b;
                    iterations++;
                }
                double mean = std::sqrt(a * b);
                result["result"] = mean;
                result["is_error"] = false;
            }
        }
        co_return result;
    }

    AsyncTask<JSON> weighted_arithmetic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto weights_opt = parse_number_array(args, "weights");
        if (!values_opt || !weights_opt)
        {
            result["error"] = "Missing or invalid 'values' or 'weights' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty() || weights_opt->empty())
        {
            result["error"] = "'values' and 'weights' arrays cannot be empty.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != weights_opt->size())
        {
            result["error"] = "'values' and 'weights' arrays must have the same length.";
            result["is_error"] = true;
        }
        else
        {
            double sum_wv{0.0};
            double sum_w{0.0};
            bool valid{true};
            for (std::size_t i = 0; i < values_opt->size(); ++i)
            {
                double w = (*weights_opt)[i];
                if (w < 0.0) { valid = false; break; }
                sum_wv += w * (*values_opt)[i];
                sum_w += w;
            }
            if (!valid)
            {
                result["error"] = "Weights must be non-negative.";
                result["is_error"] = true;
            }
            else if (sum_w == 0.0)
            {
                result["error"] = "Sum of weights is zero.";
                result["is_error"] = true;
            }
            else
            {
                double mean = sum_wv / sum_w;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> weighted_geometric_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto weights_opt = parse_number_array(args, "weights");
        if (!values_opt || !weights_opt)
        {
            result["error"] = "Missing or invalid 'values' or 'weights' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty() || weights_opt->empty())
        {
            result["error"] = "'values' and 'weights' arrays cannot be empty.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != weights_opt->size())
        {
            result["error"] = "'values' and 'weights' arrays must have the same length.";
            result["is_error"] = true;
        }
        else
        {
            double sum_wlogv{0.0};
            double sum_w{0.0};
            bool valid{true};
            for (std::size_t i = 0; i < values_opt->size(); ++i)
            {
                double v = (*values_opt)[i];
                double w = (*weights_opt)[i];
                if (v <= 0.0 || w < 0.0) { valid = false; break; }
                sum_wlogv += w * std::log(v);
                sum_w += w;
            }
            if (!valid)
            {
                result["error"] = "Weighted geometric mean requires positive values and non-negative weights.";
                result["is_error"] = true;
            }
            else if (sum_w == 0.0)
            {
                result["error"] = "Sum of weights is zero.";
                result["is_error"] = true;
            }
            else
            {
                double mean = std::exp(sum_wlogv / sum_w);
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> weighted_harmonic_mean(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        auto weights_opt = parse_number_array(args, "weights");
        if (!values_opt || !weights_opt)
        {
            result["error"] = "Missing or invalid 'values' or 'weights' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty() || weights_opt->empty())
        {
            result["error"] = "'values' and 'weights' arrays cannot be empty.";
            result["is_error"] = true;
        }
        else if (values_opt->size() != weights_opt->size())
        {
            result["error"] = "'values' and 'weights' arrays must have the same length.";
            result["is_error"] = true;
        }
        else
        {
            double sum_w{0.0};
            double sum_w_over_v{0.0};
            bool valid{true};
            for (std::size_t i = 0; i < values_opt->size(); ++i)
            {
                double v = (*values_opt)[i];
                double w = (*weights_opt)[i];
                if (v == 0.0 || w < 0.0) { valid = false; break; }
                sum_w += w;
                sum_w_over_v += w / v;
            }
            if (!valid)
            {
                result["error"] = "Weighted harmonic mean requires non-zero values and non-negative weights.";
                result["is_error"] = true;
            }
            else if (sum_w_over_v == 0.0)
            {
                result["error"] = "Division by zero in weighted harmonic mean.";
                result["is_error"] = true;
            }
            else
            {
                double mean = sum_w / sum_w_over_v;
                if (!std::isfinite(mean))
                {
                    result["error"] = "Result is not finite (overflow).";
                    result["is_error"] = true;
                }
                else
                {
                    result["result"] = mean;
                    result["is_error"] = false;
                }
            }
        }
        co_return result;
    }

    AsyncTask<JSON> variance(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double mean{0.0};
            if (args.contains("mean") && args["mean"].is_number())
            {
                mean = args["mean"].get_number();
            }
            else
            {
                mean = std::reduce(values_opt->cbegin(), values_opt->cend(), 0.0, std::plus<double>()) / static_cast<double>(values_opt->size());
            }
            double sq_sum{0.0};
            for (double v : *values_opt) { sq_sum += (v - mean) * (v - mean); }
            double var = sq_sum / static_cast<double>(values_opt->size());
            result["result"] = var;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> standard_deviation(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->empty())
        {
            result["error"] = "'values' array is empty.";
            result["is_error"] = true;
        }
        else
        {
            double mean{0.0};
            if (args.contains("mean") && args["mean"].is_number())
            {
                mean = args["mean"].get_number();
            }
            else
            {
                mean = std::reduce(values_opt->cbegin(), values_opt->cend(), 0.0, std::plus<double>()) / static_cast<double>(values_opt->size());
            }
            double sq_sum{0.0};
            for (double v : *values_opt) { sq_sum += (v - mean) * (v - mean); }
            double var = sq_sum / static_cast<double>(values_opt->size());
            double std_dev = std::sqrt(var);
            result["result"] = std_dev;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> sample_variance(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() < 2)
        {
            result["error"] = "Sample variance requires at least 2 values.";
            result["is_error"] = true;
        }
        else
        {
            double mean{0.0};
            if (args.contains("mean") && args["mean"].is_number())
            {
                mean = args["mean"].get_number();
            }
            else
            {
                mean = std::reduce(values_opt->cbegin(), values_opt->cend(), 0.0, std::plus<double>()) / static_cast<double>(values_opt->size());
            }
            double sq_sum{0.0};
            for (double v : *values_opt) { sq_sum += (v - mean) * (v - mean); }
            double var = sq_sum / static_cast<double>(values_opt->size() - 1);
            result["result"] = var;
            result["is_error"] = false;
        }
        co_return result;
    }

    AsyncTask<JSON> sample_standard_deviation(const JSON& args)
    {
        JSON result;
        auto values_opt = parse_number_array(args, "values");
        if (!values_opt)
        {
            result["error"] = "Missing or invalid 'values' array.";
            result["is_error"] = true;
        }
        else if (values_opt->size() < 2)
        {
            result["error"] = "Sample standard deviation requires at least 2 values.";
            result["is_error"] = true;
        }
        else
        {
            double mean{0.0};
            if (args.contains("mean") && args["mean"].is_number())
            {
                mean = args["mean"].get_number();
            }
            else
            {
                mean = std::reduce(values_opt->cbegin(), values_opt->cend(), 0.0, std::plus<double>()) / static_cast<double>(values_opt->size());
            }
            double sq_sum{0.0};
            for (double v : *values_opt) { sq_sum += (v - mean) * (v - mean); }
            double var = sq_sum / static_cast<double>(values_opt->size() - 1);
            double std_dev = std::sqrt(var);
            result["result"] = std_dev;
            result["is_error"] = false;
        }
        co_return result;
    }
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "math_server (MCP STDIO) running...\n";

    MCPServer server;

    // Original schema string
    std::string schema_str = R"(
        [
            {"type":"function","function":{"name":"calculate_statistics","description":"Calculates exact statistical metrics for a list of numbers.","parameters":{"type":"object","properties":{"numbers":{"type":"array","items":{"type":"number"},"description":"The array of numbers to analyze."}},"required":["numbers"]}}},
            {"type":"function","function":{"name":"arithmetic_operation","description":"Performs exact arithmetic on two numbers.","parameters":{"type":"object","properties":{"operation":{"type":"string","description":"The operation to perform.","enum":["add","subtract","multiply","divide","modulo","power"]},"a":{"type":"number","description":"The first operand."},"b":{"type":"number","description":"The second operand."}},"required":["operation","a","b"]}}},
            {"type":"function","function":{"name":"random_uniform_int","description":"Generates random integers uniformly distributed on the closed interval [min_val, max_val].","parameters":{"type":"object","properties":{"min_val":{"type":"integer","description":"The minimum value (inclusive)."},"max_val":{"type":"integer","description":"The maximum value (inclusive)."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["min_val","max_val"]}}},
            {"type":"function","function":{"name":"random_uniform_real","description":"Generates random floating-point numbers uniformly distributed on the half-open interval [min_val, max_val).","parameters":{"type":"object","properties":{"min_val":{"type":"number","description":"The minimum value (inclusive)."},"max_val":{"type":"number","description":"The maximum value (exclusive)."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["min_val","max_val"]}}},
            {"type":"function","function":{"name":"random_bernoulli","description":"Generates random boolean values (0 or 1) according to the Bernoulli distribution with probability p.","parameters":{"type":"object","properties":{"p":{"type":"number","description":"Probability of the experiment returning true (1). Must be between 0.0 and 1.0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["p"]}}},
            {"type":"function","function":{"name":"random_binomial","description":"Generates random integers according to the binomial distribution based on t trials and success probability p.","parameters":{"type":"object","properties":{"t":{"type":"integer","description":"The number of trials."},"p":{"type":"number","description":"Probability of success for each trial. Must be between 0.0 and 1.0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["t","p"]}}},
            {"type":"function","function":{"name":"random_geometric","description":"Generates random integers according to the geometric distribution: the number of trials before the first success with probability p.","parameters":{"type":"object","properties":{"p":{"type":"number","description":"Probability of success for each trial. Must be between 0.0 and 1.0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["p"]}}},
            {"type":"function","function":{"name":"random_negative_binomial","description":"Generates random integers according to the negative binomial distribution: the number of failures before k successes occur, with success probability p.","parameters":{"type":"object","properties":{"k":{"type":"integer","description":"The number of successes to achieve."},"p":{"type":"number","description":"Probability of success for each trial. Must be between 0.0 and 1.0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["k","p"]}}},
            {"type":"function","function":{"name":"random_poisson","description":"Generates random integers according to the Poisson distribution with mean (lambda) parameter.","parameters":{"type":"object","properties":{"mean":{"type":"number","description":"The mean (lambda) of the distribution. Must be non-negative."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["mean"]}}},
            {"type":"function","function":{"name":"random_exponential","description":"Generates random numbers according to the exponential distribution with rate parameter lambda.","parameters":{"type":"object","properties":{"lambda":{"type":"number","description":"The lambda parameter (rate). Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["lambda"]}}},
            {"type":"function","function":{"name":"random_gamma","description":"Generates random numbers according to the gamma distribution with shape (alpha) and scale (beta) parameters.","parameters":{"type":"object","properties":{"alpha":{"type":"number","description":"The shape parameter. Must be greater than 0."},"beta":{"type":"number","description":"The scale parameter. Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["alpha","beta"]}}},
            {"type":"function","function":{"name":"random_weibull","description":"Generates random numbers according to the Weibull distribution with shape (a) and scale (b) parameters.","parameters":{"type":"object","properties":{"shape":{"type":"number","description":"The shape parameter (a). Must be greater than 0."},"scale":{"type":"number","description":"The scale parameter (b). Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["shape","scale"]}}},
            {"type":"function","function":{"name":"random_extreme_value","description":"Generates random numbers according to the extreme value distribution (Gumbel Type I) with location (a) and scale (b) parameters.","parameters":{"type":"object","properties":{"location":{"type":"number","description":"The location parameter (a)."},"scale":{"type":"number","description":"The scale parameter (b). Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["location","scale"]}}},
            {"type":"function","function":{"name":"random_normal","description":"Generates random numbers according to the normal (Gaussian) distribution.","parameters":{"type":"object","properties":{"mean":{"type":"number","description":"The mean (mu) of the distribution."},"stddev":{"type":"number","description":"The standard deviation (sigma). Must be >= 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["mean","stddev"]}}},
            {"type":"function","function":{"name":"random_lognormal","description":"Generates random numbers according to the lognormal distribution.","parameters":{"type":"object","properties":{"mean":{"type":"number","description":"The mean of the underlying normal distribution (m)."},"stddev":{"type":"number","description":"The standard deviation of the underlying normal distribution (s). Must be >= 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["mean","stddev"]}}},
            {"type":"function","function":{"name":"random_chi_squared","description":"Generates random numbers according to the Chi-squared distribution with n degrees of freedom.","parameters":{"type":"object","properties":{"n":{"type":"number","description":"The degrees of freedom. Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["n"]}}},
            {"type":"function","function":{"name":"random_cauchy","description":"Generates random numbers according to the Cauchy distribution (Lorentz distribution).","parameters":{"type":"object","properties":{"location":{"type":"number","description":"The location parameter (a), specifying the peak."},"scale":{"type":"number","description":"The scale parameter (b), specifying the half-width at half-maximum. Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["location","scale"]}}},
            {"type":"function","function":{"name":"random_fisher_f","description":"Generates random numbers according to the Fisher F distribution.","parameters":{"type":"object","properties":{"m":{"type":"number","description":"The degrees of freedom for the numerator. Must be greater than 0."},"n":{"type":"number","description":"The degrees of freedom for the denominator. Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["m","n"]}}},
            {"type":"function","function":{"name":"random_student_t","description":"Generates random numbers according to the Student t distribution.","parameters":{"type":"object","properties":{"n":{"type":"number","description":"The degrees of freedom. Must be greater than 0."},"count":{"type":"integer","description":"How many random numbers to generate. Defaults to 1, capped at 100000."}},"required":["n"]}}},
            {"type":"function","function":{"name":"arithmetic_mean","description":"Calculates the arithmetic mean (average).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"geometric_mean","description":"Calculates the geometric mean. Requires non-negative numbers.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"harmonic_mean","description":"Calculates the harmonic mean (subcontrary_mean). Requires non-zero numbers.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"generalized_mean","description":"Calculates the generalized mean (power_mean, holder_mean, p_mean, lp_mean).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"p":{"type":"number","description":"The power parameter p. p=1 is arithmetic, p=0 is geometric, p=-1 is harmonic, p=2 is quadratic."}},"required":["values","p"]}}},
            {"type":"function","function":{"name":"weighted_generalized_mean","description":"Calculates the weighted generalized mean (weighted_power_mean, weighted_holder_mean).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"weights":{"type":"array","items":{"type":"number"},"description":"The weights for the values."},"p":{"type":"number","description":"The power parameter p."}},"required":["values","weights","p"]}}},
            {"type":"function","function":{"name":"lehmer_mean","description":"Calculates the Lehmer mean.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"p":{"type":"number","description":"The power parameter p."}},"required":["values","p"]}}},
            {"type":"function","function":{"name":"weighted_lehmer_mean","description":"Calculates the weighted Lehmer mean.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"weights":{"type":"array","items":{"type":"number"},"description":"The weights for the values."},"p":{"type":"number","description":"The power parameter p."}},"required":["values","weights","p"]}}},
            {"type":"function","function":{"name":"contraharmonic_mean","description":"Calculates the contraharmonic mean (antiharmonic_mean, anti_harmonic_mean, anti_average). Lehmer mean with p=2.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"quadratic_mean","description":"Calculates the quadratic mean (root_mean_square, rms). Generalized mean with p=2.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"cubic_mean","description":"Calculates the cubic mean. Generalized mean with p=3.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"midrange_mean","description":"Calculates the midrange mean (midpoint, midextreme). Average of min and max.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"trimean","description":"Calculates the Tukey trimean. (q1 + 2*median + q3) / 4.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"interquartile_mean","description":"Calculates the interquartile mean (iqm, midmean). Mean of the middle 50% of values.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."}},"required":["values"]}}},
            {"type":"function","function":{"name":"trimmed_mean","description":"Calculates the trimmed mean (truncated_mean, trimmed_average). Discards a percentage of extremes.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"trim_percent":{"type":"number","description":"Percentage of extremes to discard (0-50)."}},"required":["values","trim_percent"]}}},
            {"type":"function","function":{"name":"winsorized_mean","description":"Calculates the winsorized mean. Replaces a percentage of extremes with boundary values.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"winsor_percent":{"type":"number","description":"Percentage of extremes to winsorize (0-50)."}},"required":["values","winsor_percent"]}}},
            {"type":"function","function":{"name":"logarithmic_mean","description":"Calculates the logarithmic mean. Strictly defined for exactly 2 numbers.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers (must be exactly 2)."}},"required":["values"]}}},
            {"type":"function","function":{"name":"arithmetic_geometric_mean","description":"Calculates the arithmetic-geometric mean (agm). Strictly defined for exactly 2 numbers.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers (must be exactly 2)."}},"required":["values"]}}},
            {"type":"function","function":{"name":"geometric_harmonic_mean","description":"Calculates the geometric-harmonic mean (ghm). Strictly defined for exactly 2 numbers.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers (must be exactly 2)."}},"required":["values"]}}},
            {"type":"function","function":{"name":"weighted_arithmetic_mean","description":"Calculates the weighted arithmetic mean (weighted_average).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"weights":{"type":"array","items":{"type":"number"},"description":"The weights for the values."}},"required":["values","weights"]}}},
            {"type":"function","function":{"name":"weighted_geometric_mean","description":"Calculates the weighted geometric mean.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"weights":{"type":"array","items":{"type":"number"},"description":"The weights for the values."}},"required":["values","weights"]}}},
            {"type":"function","function":{"name":"weighted_harmonic_mean","description":"Calculates the weighted harmonic mean.","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"weights":{"type":"array","items":{"type":"number"},"description":"The weights for the values."}},"required":["values","weights"]}}},
            {"type":"function","function":{"name":"variance","description":"Calculates the population variance (biased_variance).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"mean":{"type":"number","description":"Optional precomputed mean to save computation."}},"required":["values"]}}},
            {"type":"function","function":{"name":"standard_deviation","description":"Calculates the population standard deviation (biased_stddev).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"mean":{"type":"number","description":"Optional precomputed mean to save computation."}},"required":["values"]}}},
            {"type":"function","function":{"name":"sample_variance","description":"Calculates the sample variance (unbiased_variance) using Bessel's correction (n-1).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"mean":{"type":"number","description":"Optional precomputed mean to save computation."}},"required":["values"]}}},
            {"type":"function","function":{"name":"sample_standard_deviation","description":"Calculates the sample standard deviation (unbiased_stddev) using Bessel's correction (n-1).","parameters":{"type":"object","properties":{"values":{"type":"array","items":{"type":"number"},"description":"The array of numbers."},"mean":{"type":"number","description":"Optional precomputed mean to save computation."}},"required":["values"]}}}
        ]
    )";

    auto parsed_schema = pooriayousefi::json::parse(schema_str);
    if (!parsed_schema)
    {
        std::cerr << "Failed to parse internal schema.\n";
        return EXIT_FAILURE;
    }

    JSONArray& schema_array = parsed_schema->get_array();

    using ToolFunc = AsyncTask<JSON>(*)(const JSON&);
    std::unordered_map<std::string, ToolFunc> handlers = {
        {"random_uniform_int", &pooriayousefi::tool_random_uniform_int},
        {"random_uniform_real", &pooriayousefi::tool_random_uniform_real},
        {"random_bernoulli", &pooriayousefi::tool_random_bernoulli},
        {"random_binomial", &pooriayousefi::tool_random_binomial},
        {"random_geometric", &pooriayousefi::tool_random_geometric},
        {"random_negative_binomial", &pooriayousefi::tool_random_negative_binomial},
        {"random_poisson", &pooriayousefi::tool_random_poisson},
        {"random_exponential", &pooriayousefi::tool_random_exponential},
        {"random_gamma", &pooriayousefi::tool_random_gamma},
        {"random_weibull", &pooriayousefi::tool_random_weibull},
        {"random_extreme_value", &pooriayousefi::tool_random_extreme_value},
        {"random_normal", &pooriayousefi::tool_random_normal},
        {"random_lognormal", &pooriayousefi::tool_random_lognormal},
        {"random_chi_squared", &pooriayousefi::tool_random_chi_squared},
        {"random_cauchy", &pooriayousefi::tool_random_cauchy},
        {"random_fisher_f", &pooriayousefi::tool_random_fisher_f},
        {"random_student_t", &pooriayousefi::tool_random_student_t},
        {"calculate_statistics", &pooriayousefi::tool_calculate_statistics},
        {"arithmetic_operation", &pooriayousefi::tool_arithmetic_operation},
        {"arithmetic_mean", &pooriayousefi::arithmetic_mean},
        {"geometric_mean", &pooriayousefi::geometric_mean},
        {"harmonic_mean", &pooriayousefi::harmonic_mean},
        {"generalized_mean", &pooriayousefi::generalized_mean},
        {"weighted_generalized_mean", &pooriayousefi::weighted_generalized_mean},
        {"lehmer_mean", &pooriayousefi::lehmer_mean},
        {"weighted_lehmer_mean", &pooriayousefi::weighted_lehmer_mean},
        {"contraharmonic_mean", &pooriayousefi::contraharmonic_mean},
        {"quadratic_mean", &pooriayousefi::quadratic_mean},
        {"cubic_mean", &pooriayousefi::cubic_mean},
        {"midrange_mean", &pooriayousefi::midrange_mean},
        {"trimean", &pooriayousefi::trimean},
        {"interquartile_mean", &pooriayousefi::interquartile_mean},
        {"trimmed_mean", &pooriayousefi::trimmed_mean},
        {"winsorized_mean", &pooriayousefi::winsorized_mean},
        {"logarithmic_mean", &pooriayousefi::logarithmic_mean},
        {"arithmetic_geometric_mean", &pooriayousefi::arithmetic_geometric_mean},
        {"geometric_harmonic_mean", &pooriayousefi::geometric_harmonic_mean},
        {"weighted_arithmetic_mean", &pooriayousefi::weighted_arithmetic_mean},
        {"weighted_geometric_mean", &pooriayousefi::weighted_geometric_mean},
        {"weighted_harmonic_mean", &pooriayousefi::weighted_harmonic_mean},
        {"variance", &pooriayousefi::variance},
        {"standard_deviation", &pooriayousefi::standard_deviation},
        {"sample_variance", &pooriayousefi::sample_variance},
        {"sample_standard_deviation", &pooriayousefi::sample_standard_deviation}
    };

    for (const auto& item : schema_array)
    {
        if (item.contains("function"))
        {
            MCPTool tool;
            tool.name = item["function"]["name"].get_string();
            tool.description = item["function"]["description"].get_string();
            tool.parameters_schema = item["function"]["parameters"];

            auto it = handlers.find(tool.name);
            if (it != handlers.end())
            {
                auto func = it->second;
                server.register_tool(std::move(tool), [func](const JSON& args) -> AsyncTask<JSON> {
                    co_return co_await func(args);
                });
            }
        }
    }

    try
    {
        sync_wait(server.start());
    }
    catch (const std::exception& e)
    {
        std::cerr << "Server crashed: " << e.what() << std::endl;
        result = EXIT_FAILURE;
    }

    return result;
}