#ifndef XTCL_ERROR_H
#define XTCL_ERROR_H

#include <utility>
#include <functional>
#include <expected>
#include <sstream>

#include <concepts>

using namespace std::literals;

namespace Xtcl
{
    template <typename M>
    concept Msg = std::invocable<M, std::ostream &>;

    class Error
    {
        std::function<void (std::ostream &)> msg;

    public:

        Error() : msg {} {};

        template <Msg M>
        Error(M && msg) : msg {std::forward<M>(msg)} {}

        Error(Error const &) = default;
        Error(Error &&) = default;

        Error & operator = (Error const &) = default;
        Error & operator = (Error &&) = default;

        void operator () (std::ostream & os) const
        {
            if (msg)
            {
                msg(os);
            }
        }

        static auto index(Error & error, std::size_t index)
        {
            return std::unexpected
            {
                [error = std::move(error), index] (std::ostream & os)
                {
                    os << '[' << index << "] "sv;
                    error(os);
                }
            };
        }

        static auto forward(Error & error)
        {
            return std::unexpected {std::move(error)};
        }

        template <Msg M>
        static auto generic(M && msg)
        {
            return std::unexpected {Error {std::forward<M>(msg)}};
        }

        template <std::convertible_to<std::string> T>
        static auto text(T && msg)
        {
            return std::unexpected
            {
                Error
                {
                    [msg = std::string {std::forward<T>(msg)}] (std::ostream &os)
                    {
                        os << msg;
                    }
                }
            };
        }
    };
}

std::ostream & operator << (std::ostream & os, Xtcl::Error const & error)
{
    error(os);
    return os;
}

#endif
