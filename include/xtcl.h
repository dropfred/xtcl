#ifndef XTCL_H
#define XTCL_H

#include <tcl/tcl.h>

#include <utility>
#include <type_traits>
#include <ranges>
#include <functional>
#include <expected>
#include <string>
#include <tuple>

// error handling
#include <sstream>

#include "xtcl_def.h"
#include "xtcl_type.h"
#include "xtcl_error.h"

using namespace std::literals;
using namespace std::string_literals;

namespace Xtcl
{
    namespace detail
    {
        using TclResult = Result<int>;

        using Function = std::function<TclResult (Tcl_Interp *, int, Tcl_Obj * const [])>;

        template<std::size_t S>
        struct CmdData
        {
            std::array<Function, S> fns;
        };

#ifdef XTCL_GCC_FIX_N4659_17_7_3__2
        template <typename T>
        struct FunctionReturn
        {
            static ToResult to(Tcl_Interp * tcl, T const & value)
            {
                return Xtcl::to(tcl, value);
            }
        };

        template <typename T>
        struct FunctionReturn<Result<T>>
        {
            static ToResult to(Tcl_Interp * tcl, Result<T> & value)
            {
                if (not value)
                {
                    return Error::forward(value.error());
                }

                return Xtcl::to(tcl, *value);
            }
        };

#if XTCL_SUPPORT_POINTER
        template <typename T>
        struct FunctionReturn<T *>
        {
            static ToResult to(Tcl_Interp * tcl, T * const value)
            {
                return Xtcl::to(tcl, *value);
            }
        };
#endif

#if XTCL_SUPPORT_CSTRING
        template <>
        struct FunctionReturn<char const *>
        {
            static ToResult to(Tcl_Interp * tcl, char const * value)
            {
                return Xtcl::to(tcl, value);
            }
        };
#endif

        template <typename T>
        struct FunctionArg
        {
            static T && forward(T & v) {return std::move(v);}
        };

        template <typename T>
        struct FunctionArg<T &>
        {
            static T & forward(T & v) {return v;}
        };

        template <typename T>
        struct FunctionArg<T &&>
        {
            static T && forward(T & v) {return std::move(v);}
        };

#if XTCL_SUPPORT_POINTER
        template <typename T>
        struct FunctionArg<T *>
        {
            static T * forward(T & v) {return &v;}
        };
#endif

#if XTCL_SUPPORT_CSTRING
        template <>
        struct FunctionArg<char const *>
        {
            static char const * forward(char const *s) {return s;}
        };
#endif
#endif
        template <typename R, typename... As>
        struct FunctionHelper
        {
#ifdef XTCL_GCC_FIX_N4659_17_7_3__2
            template <typename T> using Return = FunctionReturn<T>;
            template <typename T> using Arg = FunctionArg<T>;
#else
            template <typename T>
            struct Return
            {
                static ToResult to(Tcl_Interp* tcl, T const& value)
                {
                    return Xtcl::to(tcl, value);
                }
            };

            template <typename T>
            struct Return<Result<T>>
            {
                static ToResult to(Tcl_Interp* tcl, Result<T>& value)
                {
                    if (not value)
                    {
                        return Error::forward(value.error());
                    }

                    return Xtcl::to(tcl, *value);
                }
            };

#if XTCL_SUPPORT_POINTER
            template <typename T>
            struct Return<T*>
            {
                static ToResult to(Tcl_Interp* tcl, T* const value)
                {
                    return Xtcl::to(tcl, *value);
                }
            };
#endif

#if XTCL_SUPPORT_CSTRING
            template <>
            struct Return<char const*>
            {
                static ToResult to(Tcl_Interp* tcl, char const* value)
                {
                    return Xtcl::to(tcl, value);
                }
            };
#endif

            template <typename T>
            struct Arg
            {
                static T&& forward(T& v) { return std::move(v); }
            };

            template <typename T>
            struct Arg<T&>
            {
                static T& forward(T& v) { return v; }
            };

            template <typename T>
            struct Arg<T&&>
            {
                static T&& forward(T& v) { return std::move(v); }
            };

#if XTCL_SUPPORT_POINTER
            template <typename T>
            struct Arg<T*>
            {
                static T* forward(T& v) { return &v; }
            };
#endif

#if XTCL_SUPPORT_CSTRING
            template <>
            struct Arg<char const*>
            {
                static char const* forward(char const* s) { return s; }
            };
#endif
#endif

            static constexpr std::size_t const S {sizeof... (As)};

            template <std::size_t... Is>
            static Function make(std::function<R (As...)> && fn, std::index_sequence<Is...>)
            {
                using F = std::function<R (As...)>;
                using Tuple = detail::Tuple<As...>;

                return Function
                {
                    [fn = std::move(fn)] (Tcl_Interp * tcl, int objc, Tcl_Obj * const objv []) -> TclResult
                    {
                        Tcl_ResetResult(tcl);

                        auto args = Tuple::from(tcl, objc, objv);

                        if (not args)
                        {
                            return Error::generic
                            (
                                [error = std::move(args.error()), name = std::string(Tcl_GetString(objv[-1]))]
                                (std::ostream & os)
                                {
                                    os << name;
                                    if constexpr (S != 0) {os << ' ' << Tuple {};}
                                    os << ": "sv << error;
                                });
                        }

                        if constexpr (std::is_void_v<R>)
                        {
                            fn(Arg<As>::forward(std::get<Is>(*args))...);

                            return TCL_OK;
                        }
                        else
                        {
                            auto r = Return<R>::to(tcl, fn(Arg<As>::forward(std::get<Is>(*args))...));

                            if (not r)
                            {
                                return Error::forward(r.error());
                            }

                            Tcl_SetObjResult(tcl, *r);

                            return TCL_OK;
                        }
                    }
                };
            }

            static Function make(std::function<R (As...)> && f)
            {
                return make(std::move(f), std::index_sequence_for<As...> {});
            }
        };

        template <typename R, typename... As>
        auto make_function(std::function<R (As...)> && f)
        {
            return FunctionHelper<R, As...>::make(std::move(f));
        }

        template<std::size_t S>
        void delete_function(ClientData cdata)
        {
            delete static_cast<CmdData<S> *>(cdata);
        }

        template<std::size_t S>
        int call_function(ClientData cdata, Tcl_Interp * tcl, int objc, Tcl_Obj * const objv[])
        {
            if constexpr (S != 0)
            {
                auto & data = *static_cast<CmdData<S> *>(cdata);

                std::array<Error, S> errors {};

                for (auto const & [i, f] : std::views::enumerate(data.fns))
                {
                    auto r = f(tcl, objc - 1, objv + 1);
                    if (r)
                    {
                        return *r;
                    }
                    else
                    {
                        errors[i] = std::move(r.error());
                    }
                }

                std::ostringstream os {};
                for (auto [i, e] : std::views::enumerate(errors))
                {
                    if constexpr (S > 1)
                    {
                        if (i != 0) os << std::endl;
                    }
                    os << e;
                }

                Tcl_SetObjResult(tcl, Tcl_NewStringObj(os.str().c_str(), -1));

                return TCL_ERROR;
            }
            else
            {
                return TCL_OK;
            }
        }

        template <typename... Fs>
        void add_function(Tcl_Interp * tcl, char const * name, Fs && ... fs)
        {
            constexpr std::size_t const S {sizeof... (Fs)};

            auto * data = new CmdData<S>
            {
                .fns = {make_function(std::function {std::forward<Fs>(fs)})...}
            };

            Tcl_CreateObjCommand(tcl, name, call_function<S>, data, delete_function<S>);
        }
    }

    template <typename... Fs>
    void add_function(Tcl_Interp * tcl, char const * name, Fs && ... fs)
    {
        detail::add_function(tcl, name, std::forward<Fs>(fs)...);
    }

    template <typename... Fs>
    void add_function(Tcl_Interp * tcl, std::string const & name, Fs && ... fs)
    {
        detail::add_function(tcl, name.c_str(), std::forward<Fs>(fs)...);
    }
}

#endif
