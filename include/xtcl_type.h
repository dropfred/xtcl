#ifndef XTCL_TYPE_H
#define XTCL_TYPE_H

#include <tcl/tcl.h>

#include <utility>
#include <type_traits>
#include <ranges>
#include <expected>
#include <string_view>
#include <string>
#include <tuple>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

// error handling
#include <format>
#include <sstream>
#include <limits>

#include "xtcl_def.h"
#include "xtcl_error.h"

using namespace std::literals;
using namespace std::string_literals;

namespace Xtcl
{
    template <typename T>
    using Value =
#if XTCL_SUPPORT_CSTRING
    std::conditional_t
    <
        std::is_same_v<std::remove_cvref_t<T>, char const *>,
        char const *,
#endif
        std::remove_cvref_t
        <
#if XTCL_SUPPORT_POINTER
            std::remove_pointer_t<std::remove_cvref_t<T>>
#else
            T
#endif
        >
#if XTCL_SUPPORT_CSTRING
    >
#endif
    ;

    template <typename T>
    using Result = std::expected<T, Error>;

    template <typename T>
    using FromResult = Result<T>;

    using ToResult = Result<Tcl_Obj *>;

    template <typename>
    struct Type
    {
        static_assert(false, "unsupported type");
    };

    template <typename T>
    auto from(Tcl_Interp * tcl, Tcl_Obj * obj)
    {
        return Type<Value<T>>::from(tcl, obj);
    }

    template <typename T>
    auto to(Tcl_Interp * tcl, T const & value)
    {
        return Type<Value<T>>::to(tcl, value);
    }

    namespace detail
    {
        template <typename... Ts>
        class Tuple
        {
        public :

            static constexpr std::size_t const S {sizeof... (Ts)};

            using Values = std::tuple<Value<Ts>...>;
            using Names = std::array<std::string_view, S>;

        private :

            template <typename...>
            struct From;

            template <>
            struct From<>
            {
                static Xtcl::FromResult<std::tuple<>> values(Tcl_Interp * tcl, Tcl_Obj * const objv[])
                {
                    return {};
                }
            };

            template <typename V, typename... Vs>
            struct From<V, Vs...>
            {
                static Xtcl::FromResult<std::tuple<V, Vs...>> values(Tcl_Interp * tcl, Tcl_Obj * const objv[])
                {
                    auto v = Xtcl::from<V>(tcl, objv[0]);
                    if (not v)
                    {
                        return Error::index(v.error(), S - (sizeof... (Vs) + 1));
                    }

                    auto vs = From<Vs...>::values(tcl, objv + 1);
                    if (not vs)
                    {
                        return Error::forward(vs.error());
                    }

                    return std::tuple_cat(std::make_tuple(std::move(*v)), std::move(*vs));
                }
            };

            template <std::size_t I>
            static ToResult to(Tcl_Interp * tcl, Tcl_Obj * obj, std::tuple<Ts...> const & values)
            {
                if constexpr (I != S)
                {
                    static_assert(I < S);

                    auto v = Xtcl::to(tcl, std::get<I>(values));

                    if (not v)
                    {
                        Tcl_DecrRefCount(obj);
                        return Error::index(v.error(), I);
                    }

                    Tcl_ListObjAppendElement(tcl, obj, *v);

                    return to<I + 1>(tcl, obj, values);
                }
                else
                {
                    return obj;
                }
            }

        public:

            static Names names()
            {
                return {Type<Value<Ts>>::name()...};
            }

            static FromResult<Values> from(Tcl_Interp * tcl, int objc, Tcl_Obj * const objv[])
            {
                if (objc != S)
                {
                    return Error::generic
                    (
                        [objc] (std::ostream &os)
                        {
                            os << "wrong # args (expected "sv << S << " but got "sv << objc << ')';
                        }
                    );
                }

                return From<Value<Ts>...>::values(tcl, objv);
            }

            static ToResult to(Tcl_Interp * tcl, std::tuple<Ts...> const & values)
            {
                return to<0>(tcl, Tcl_NewListObj(0, nullptr), values);
            }
        };

        template <typename N>
        static auto type_error(Tcl_Obj * obj)
        {
            struct Obj
            {
                Tcl_Obj * ptr;
                Obj(Tcl_Obj * ptr) : ptr {ptr} {Tcl_IncrRefCount(ptr);}
                ~Obj() {Tcl_DecrRefCount(ptr);}
                Obj(Obj const & obj) : Obj(obj.ptr) {}
                Obj(Obj && obj) : Obj(obj.ptr) {}
                Obj & operator = (Obj const &) = delete;
                Obj & operator = (Obj &&) = delete;
            };

            return std::unexpected
            {
                Error
                {
                    [obj = Obj(obj)] (std::ostream & os)
                    {
                        os << "expected "sv << Type<N>::name() << " but got \""sv << Tcl_GetStringFromObj(obj.ptr, nullptr) << '"';
                    }
                }
            };
        }

        template <typename N>
        requires std::is_integral_v<N>
        struct IntegralType
        {
            static FromResult<N> from(Tcl_Interp * tcl, Tcl_Obj * obj)
            {
                Tcl_WideInt value;

                if (Tcl_GetWideIntFromObj(tcl, obj, &value) != TCL_OK)
                {
#if XTCL_ERROR_TCL
                    return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                    return type_error<N>(obj);
#endif
                }

#if XTCL_ERROR_OVERFLOW
                if constexpr (sizeof (N) < sizeof (Tcl_WideInt))
                {
                    if (value > std::numeric_limits<N>::max())
                    {
                        return Error::generic
                        (
                            [value] (std::ostream & os)
                            {
                                os << "overflow (highest "sv << Type<N>::name() << " value is "sv << +std::numeric_limits<N>::max() << " but got "sv << value << ')';
                            }
                        );
                    }
                    else if (value < std::numeric_limits<N>::lowest())
                    {
                        return Error::generic
                        (
                            [value] (std::ostream & os)
                            {
                                os << "underflow (lowest "sv << Type<N>::name() << " value is "sv << +std::numeric_limits<N>::lowest() << " but got "sv << value << ')';
                            }
                        );
                    }
                }
#endif

                return N(value);
            }

            static ToResult to(Tcl_Interp * tcl, N value)
            {
                return Tcl_NewWideIntObj(Tcl_WideInt(value));
            }
        };

        template <typename N>
        requires std::is_floating_point_v<N>
        struct FloatingType
        {
            static FromResult<N> from(Tcl_Interp * tcl, Tcl_Obj * obj)
            {
                double value;

                if (Tcl_GetDoubleFromObj(tcl, obj, &value) != TCL_OK)
                {
#if XTCL_ERROR_TCL
                    return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                    return type_error<N>(obj);
#endif
                }

                return N(value);
            }

            static ToResult to(Tcl_Interp * tcl, N value)
            {
                return Tcl_NewDoubleObj(double(value));
            }
        };

        template <template <typename> typename S, typename T>
        struct Set
        {
            static FromResult<S<T>> from(Tcl_Interp * tcl, Tcl_Obj * obj)
            {
                Tcl_Obj **objv;
                int objc;

                if (Tcl_ListObjGetElements(tcl, obj, &objc, &objv) != TCL_OK)
                {
#if XTCL_ERROR_TCL
                    return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                    return detail::type_error<S<T>>(obj);
#endif
                }

                S<T> set {};

                for (std::size_t i = 0; i < objc; ++i)
                {
                    auto e = Xtcl::from<T>(tcl, objv[i]);
                    if (e)
                    {
                        set.insert(std::move(*e));
                    }
                    else
                    {
                        return Error::index(e.error(), i);
                    }
                }

                return set;
            }

            static ToResult to(Tcl_Interp * tcl, S<T> const & set)
            {
                Tcl_Obj * list = Tcl_NewListObj(0, nullptr);

                for (auto & e : set)
                {
                    auto obj = Xtcl::to(tcl, e);
                    if (not obj)
                    {
                        Tcl_DecrRefCount(list);
                        return Error::forward(obj.error());
                    }
                    Tcl_ListObjAppendElement(tcl, list, *obj);
                }

                return list;
            }
        };

        template <template <typename, typename> typename M, typename K, typename V>
        struct Map
        {
            static FromResult<M<K, V>> from(Tcl_Interp * tcl, Tcl_Obj * obj)
            {
                M<K, V> map {};
                Tcl_DictSearch search;
                Tcl_Obj * key;
                Tcl_Obj * value;
                int done;
                std::size_t index {};

                if (Tcl_DictObjFirst(tcl, obj, &search, &key, &value, &done) != TCL_OK)
                {
#if XTCL_ERROR_TCL
                    return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                    return detail::type_error<M<K, V>>(obj);
#endif
                }

                while (done == 0)
                {
                    auto k = Xtcl::from<K>(tcl, key);
                    if (not k)
                    {
                        return Error::index(k.error(), index);
                    }
                    ++index;
                    auto v = Xtcl::from<V>(tcl, value);
                    if (not v)
                    {
                        return Error::index(v.error(), index);
                    }
                    ++index;

                    map[std::move(*k)] = std::move(*v);

                    Tcl_DictObjNext(&search, &key, &value, &done);
                }

                return map;
            }

            static ToResult to(Tcl_Interp * tcl, M<K, V> const & map)
            {
                Tcl_Obj * dict = Tcl_NewDictObj();

                for (auto & [key, value] : map)
                {
                    auto k = Xtcl::to(tcl, key);
                    if (not k)
                    {
                        Tcl_DecrRefCount(dict);
                        return Error::forward(k.error());
                    }
                    auto v = Xtcl::to(tcl, value);
                    if (not v)
                    {
                        Tcl_DecrRefCount(dict);
                        return Error::forward(v.error());
                    }
                    Tcl_DictObjPut(tcl, dict, *k, *v);
                }

                return dict;
            }
        };
    }
}

template <typename... As>
std::ostream & operator << (std::ostream & os, typename Xtcl::detail::Tuple<As...>)
{
    for (auto n : Xtcl::detail::Tuple<As...>::names() | std::views::join_with(' '))
    {
        os << n;
    }

    return os;
}

namespace Xtcl
{
#define XTCL_INTEGRAL_TYPE(t) template <> struct Type<t> : public detail::IntegralType<t> {static auto name() {return std::string_view {"<"#t">"};}}
    XTCL_INTEGRAL_TYPE(char);
    XTCL_INTEGRAL_TYPE(signed char);
    XTCL_INTEGRAL_TYPE(unsigned char);
    XTCL_INTEGRAL_TYPE(int);
    XTCL_INTEGRAL_TYPE(unsigned int);
    XTCL_INTEGRAL_TYPE(short int);
    XTCL_INTEGRAL_TYPE(unsigned short int);
    XTCL_INTEGRAL_TYPE(long int);
    XTCL_INTEGRAL_TYPE(unsigned long int);
    XTCL_INTEGRAL_TYPE(long long int);
    XTCL_INTEGRAL_TYPE(unsigned long long int);
#undef XTCL_INTEGRAL_TYPE

#define XTCL_FLOATING_TYPE(t) template <> struct Type<t> : public detail::FloatingType<t> {static auto name() {return std::string_view {"<"#t">"};}}
    XTCL_FLOATING_TYPE(float);
    XTCL_FLOATING_TYPE(double);
#ifdef XTCL_HAS_LONG_DOUBLE
    XTCL_FLOATING_TYPE(long double);
#endif
#undef XTCL_FLOATING_TYPE

    template <>
    struct Type<std::string>
    {
        static auto name() {return "<string>"sv;}

        static FromResult<std::string> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return Tcl_GetString(obj);
        }

        static ToResult to(Tcl_Interp * tcl, std::string const & s)
        {
            return Tcl_NewStringObj(s.data(), int(s.size()));
        }
    };

    template <>
    struct Type<std::string_view>
    {
        static auto name() {return "<string view>"sv;}

        static FromResult<std::string_view> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return Tcl_GetString(obj);
        }

        static ToResult to(Tcl_Interp * tcl, std::string_view const & s)
        {
            return Tcl_NewStringObj(s.data(), int(s.size()));
        }
    };

#if XTCL_SUPPORT_CSTRING
    template <>
    struct Type<char const *>
    {
        static auto name() {return "<cstring>"sv;}

        static FromResult<char const *> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return Tcl_GetString(obj);
        }

        static ToResult to(Tcl_Interp * tcl, char const * s)
        {
            return Tcl_NewStringObj(s, -1);
        }
    };
#endif

    template <>
    struct Type<bool>
    {
        static auto name()
        {
            return "<bool>"sv;
        }

        static FromResult<bool> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            int value;

            if (Tcl_GetBooleanFromObj (tcl, obj, &value) != TCL_OK)
            {
#if XTCL_ERROR_TCL
                return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                return detail::type_error<bool>(obj);
#endif
            }

            return (value != 0) ? true : false;
        }

        static ToResult to(Tcl_Interp * tcl, bool value)
        {
            return Tcl_NewBooleanObj(value ? 1 : 0);
        }
    };

    template <typename T>
    class Type<std::vector<T>>
    {
        static auto make_name()
        {
            return std::format("<vector {}>"sv, Type<T>::name());
        }

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static FromResult<std::vector<T>> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            Tcl_Obj **objv;
            int objc;

            if (Tcl_ListObjGetElements(tcl, obj, &objc, &objv) != TCL_OK)
            {
#if XTCL_ERROR_TCL
                return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                return detail::type_error<std::vector<T>>(obj);
#endif
            }

            std::vector<T> vec {};
            vec.reserve(objc);

            for (std::size_t i = 0; i < objc; ++i)
            {
                auto e = Xtcl::from<T>(tcl, objv[i]);
                if (e)
                {
                    vec.push_back(std::move(*e));
                }
                else
                {
                    return Error::index(e.error(), i);
                }
            }

            return vec;
        }

        static ToResult to(Tcl_Interp * tcl, std::vector<T> const & vec)
        {
            Tcl_Obj * list = Tcl_NewListObj(0, nullptr);

            for (auto & e : vec)
            {
                auto obj = Xtcl::to(tcl, e);
                if (not obj)
                {
                    Tcl_DecrRefCount(list);
                    return Error::forward(obj.error());
                }
                Tcl_ListObjAppendElement(tcl, list, *obj);
            }

            return list;
        }
    };

    template <typename T, std::size_t S>
    class Type<std::array<T, S>>
    {
        static auto make_name()
        {
            return std::format("<array {} x {}>"sv, Type<T>::name(), S);
        }

        template<size_t... Is>
        static auto to_tuple(std::array<T, S> const & array, std::index_sequence<Is...>) {return std::tuple {array[Is]...};}

        using Tuple = decltype (to_tuple(std::declval<std::array<T, S>>(), std::declval<std::make_index_sequence<S>>()));

        template<size_t... Is>
        static auto to_array(auto & tuple, std::index_sequence<Is...>) {return std::array<T, S> {std::move(std::get<Is>(tuple))...};}

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static FromResult<std::array<T, S>> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            auto tuple = Xtcl::from<Tuple>(tcl, obj);

            if (not tuple)
            {
                return Error::forward(tuple.error());
            }

            return to_array(*tuple, std::make_index_sequence<S> {});
        }

        static ToResult to(Tcl_Interp * tcl, std::array<T, S> const & array)
        {
            return Xtcl::to(tcl, to_tuple(array, std::make_index_sequence<S> {}));
        }
    };

    template <typename K, typename V>
    class Type<std::map<K, V>>
    {
        static auto make_name()
        {
            return std::format("<map {} -> {}>"sv, Type<K>::name(), Type<V>::name());
        }

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static auto from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return detail::Map<std::map, K, V>::from(tcl, obj);
        }

        static auto to(Tcl_Interp * tcl, std::map<K, V> const & map)
        {
            return detail::Map<std::map, K, V>::to(tcl, map);
        }
    };

    template <typename K, typename V>
    class Type<std::unordered_map<K, V>>
    {
        static auto make_name()
        {
            return std::format("<unordered_map {} -> {}>"sv, Type<K>::name(), Type<V>::name());
        }

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static auto from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return detail::Map<std::unordered_map, K, V>::from(tcl, obj);
        }

        static auto to(Tcl_Interp * tcl, std::unordered_map<K, V> const & map)
        {
            return detail::Map<std::unordered_map, K, V>::to(tcl, map);
        }
    };

    template <typename T>
    class Type<std::set<T>>
    {
        static auto make_name()
        {
            return std::format("<set {}>"sv, Type<T>::name());
        }

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static auto from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return detail::Set<std::set, T>::from(tcl, obj);
        }

        static auto to(Tcl_Interp * tcl, std::set<T> const & set)
        {
            return detail::Set<std::set, T>::to(tcl, set);
        }
    };

    template <typename T>
    class Type<std::unordered_set<T>>
    {
        static auto make_name()
        {
            return std::format("<unordered_set {}>"sv, Type<T>::name());
        }

    public :

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static auto from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            return detail::Set<std::unordered_set, T>::from(tcl, obj);
        }

        static auto to(Tcl_Interp * tcl, std::unordered_set<T> const & set)
        {
            return detail::Set<std::unordered_set, T>::to(tcl, set);
        }
    };

    template <typename... Ts>
    class Type<std::tuple<Ts...>>
    {
        static constexpr std::size_t const S {sizeof... (Ts)};

        using Tuple = detail::Tuple<Ts...>;

        static auto make_name()
        {
            std::ostringstream os {};
            os << "<tuple"sv;
            if constexpr (S != 0)
            {
                os << ' ' << Tuple {};
            }
            os << '>';
            return os.str();
        }

    public:

        static auto name()
        {
            static std::string const name {make_name()};
            return std::string_view {name};
        }

        static Result<std::tuple<Value<Ts>...>> from(Tcl_Interp * tcl, Tcl_Obj * obj)
        {
            Tcl_Obj **objv;
            int objc;

            if (Tcl_ListObjGetElements(tcl, obj, &objc, &objv) != TCL_OK)
            {
#if XTCL_ERROR_TCL
                return Error::text(Tcl_GetString(Tcl_GetObjResult(tcl)));
#else
                return detail::type_error<std::tuple<Value<Ts>...>>(obj);
#endif
            }

            return Tuple::from(tcl, objc, objv);
        }

        static ToResult to(Tcl_Interp * tcl, std::tuple<Ts...> const & tuple)
        {
            return Tuple::to(tcl, tuple);
        }
    };
}

#endif
