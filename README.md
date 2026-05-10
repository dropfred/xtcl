# Xtcl

C++ 23 Tcl extension helper.

Not sure exactly why I did this. I was wondering: how feasible/hard/easy would it be is to automatically call (and handle errors nicely) a function expecting a fixed number of typed arguments from an old school function `(argc, argv[])` style function expecting a variable number of untyped arguments? As many scripting languages use the `(argc, argv[])` paradigm for C extensions, I chose to do a helper for one of them as a practical exercise, and I ended up choosing Tcl because:

- Extending Tcl is super easy.
- For some reason, Tcl has a special place in my heart ❤️.

I *might* revisit it one day for Python instead of Tcl, which would certainly be more useful and shouldn't require much effort.

# Quick tour

Assume you want to add this basic function to the Tcl interpreter :

```c
float clamp(float value, float low, float high)
{
    assert(low <= high);
    return (value < low) ? low : (value > high) ? high : value;
}
```

Using the C Tcl API, you would probably end up with something looking like this:

```c
int tcl_clamp(ClientData data, Tcl_Interp * tcl, int objc, Tcl_Obj * const objv[])
{
    double value, low, high;

    if (objc != 4)
    {
        Tcl_WrongNumArgs(tcl, 1, objv, "<float> <float> <float>");
        return TCL_ERROR;
    }

    if
    (
        (Tcl_GetDoubleFromObj(tcl, objv[1], &value) != TCL_OK) ||
        (Tcl_GetDoubleFromObj(tcl, objv[2], &low  ) != TCL_OK) ||
        (Tcl_GetDoubleFromObj(tcl, objv[3], &high ) != TCL_OK)
    )
    {
        return TCL_ERROR;
    }

    value = clamp((float)value, (float)low, (float)high);

    Tcl_SetObjResult(tcl, Tcl_NewDoubleObj(value));

    return TCL_OK;
}
:
Tcl_CreateObjCommand(tcl, "clamp", tcl_clamp, NULL, NULL);
```

That's a lot of boilerplate code! And these are just 3 basic float arguments, things get worse when containers come into play.

Using `Xtcl`, the above code is implemented in a single-can't-be-simpler line:

```c++
Xtcl::add_function(tcl, "clamp", clamp);
```

`Xtcl` is not limited to functions:

- A lambda:

    ```c++
    Xtcl::add_function(tcl, "add", [] (int a, int b) {return a + b;});
    ```

- An object implementing the `()` operator:

    ```c++
    struct Add
    {
        int operator () (int a, int b) const {return a + b;}
    };
    :
    Xtcl::add_function(tcl, "add", Add {});
    ```

A procedure can be "overloaded" using several functions:

```c++
Xtcl::add_function
(
    tcl, "multi",
    [] (int n) {return "one int"sv;},
    [] (std::string const & s) {return "one string"sv;},
    [] (int x, int y) {return "two ints"sv;},
    [] (int n, std::string const & s) {return "one int and one string"sv;},
    :
);
```

# Current limitations & caveats

- Variadic functions are not supported.
- C arrays are not supported.
- Callable objects implementing more than one `()` operator are not supported.
- Default arguments are ignored.
- Tcl 8.6 does not have unsigned counterpart to `Tcl_WideInt`, and so unsigned integers greater than `std::numeric_limits<Tcl_WideInt>::max()` are stored as negative numbers.

# Few details

## Types

Out of the box supported types:

- arithmetic types: chars, ints, floats, and doubles
- strings
- booleans
- tuples
- most used containers: vectors, arrays, maps, and sets

| C++ | Tcl |
| --- | --- |
| integrals | `Tcl_WideInt` |
| floating-points | `double` |
| `bool` | boolean (`int`) |
| `std::string`<br>`string_view`<br>`C string` (optional) | string |
| `std::tuple`<br>`std::vector`<br>`std::array`<br>`std::set`<br>`std::unordered_set` | list |
| `std::map`<br>`std::unordered_map` | dictionary |

### Returning errors

So far, the functions seen in the code snipets above always set the Tcl interpreter's result to the Tcl representation of the returned value (if any), and return `TCL_OK`. When errors can occur, the function should then return a `Xtcl::Result` object:

```c++
Xtcl::Result<int> idiv(int n, int d)
{
    if (d == 0) return Xtcl::Error::text("null denominator");

    return (n / d);
}
```

### User type support

If a type is not already supported, the `Xtcl::Type` templated structure must be specialized. The `Xtcl::Type` specialization for type `T` should provide:
- A `<string like> name()` static function.
- A `Xtcl::FromResult<T> from(Tcl_Interp *, Tcl_Obj *)` static function if `T` is used as arguments.
- A `Xtcl::ToResult to(Tcl_Interp *, T const &)` static function if `T` is used as returns.

For example, let's say you want to support the structure below:

```c++
struct Vec2
{
    float x, y;
};
```

It's up to you how a C++ object is represented in Tcl, in this case an array is probably the most natural.

```c++
template <> struct Xtcl::Type<Vec2>
{
    static auto name() {return "<vec2>";}

    using TR = std::array<float, 2>;

    static Xtcl::FromResult<Vec2> from(Tcl_Interp * tcl, Tcl_Obj * obj)
    {
        auto xy = Xtcl::from<TR>(tcl, obj);

        if (not xy)
        {
            return Xtcl::Error::forward(xy.error());
        }

        auto [x, y] = *xy;

        return Vec2 {x, y};
    }

    static Xtcl::ToResult to(Tcl_Interp * tcl, Vec2 const & value)
    {
        return Xtcl::to(tcl, TR {value.x, value.y});
    }
};
```

You can now extend Tcl with functions dealing with `Vec2` objects:

```c++
Vec2 vec2_new(float x, float y)
{
    return {x, y};
}

float vec2_dot(Vec2 const & a, Vec2 const & b)
{
    return a.x * b.x + a.y * b.y;
}
:
Xtcl::add_function(tcl, "vec2_new", vec2_new);
Xtcl::add_function(tcl, "vec2_dot", vec2_dot);
```

The `Vec2` type and its associated functions are now available in Tcl:

```
% set a [vec2_new 0.0 1.0]
0.0 1.0
% set b [vec2_new 2.0 3.0]
2.0 3.0
% vec2_dot $a $b
3.0
```

### Qualification and references

As seen in the code snipets above, qualifications and references are supported, not just plain values. Mutable references are also supported, so refered values can be moved if necessary.

### C strings

If the `XTCL_SUPPORT_CSTRING` definition is enabled (default), `char const *` values are assumed to be C strings. Note that `char *` (no `const` qualification on the pointed value) are considered integers (if `XTCL_SUPPORT_POINTER` is enabled, see below), not strings.

### Pointers

If the `XTCL_SUPPORT_POINTER` definition is enabled (default), pointers are supported. The pointed value is assumed to be a single value, not an array (except for `char const *` if `XTCL_SUPPORT_CSTRING` definition is enabled, see above).

### Integers overflows

If the `XTCL_ERROR_OVERFLOW` definition is enabled (default), Tcl integer values that do not fit into the destination type are treated as errors. Otherwise, values are simply truncated in the same way as in the C language.

## Overloads

The way overloads are handled is quite dumb: functions are checked in the order they are added, and the first one whose arguments match is the one that is called. This means that the order functions are added is important:

```c++
Xtcl::add_function
(
    tcl, "ok",
    [] (int) {std::cout << "got an int\n";},
    [] (std::string const &) {std::cout << "got a string\n";}
);

Xtcl::add_function
(
    tcl, "ko",
    [] (std::string const&) {std::cout << "got a string\n";},
    [] (int) {std::cout << "got an int\n";}
);
```

In the `ko` procedure, the `int` flavored function will never be called because the `std::string` one is always valid:

```
% ok abc
got a string
% ok 123
got an int
% ko abc
got a string
% ko 123
got a string
```

Note that it is possible to define an procedure without any functions, in which case the arguments (if any) are ignored, and the procedure therefore never fails. Not very sure how it could be useful, maybe for meta-programming purpose?

## Error handling

Because of the way overloads are designed, it is expected that some functions calls fail, and this is why error handling is somewhat convoluted: error messages are costly, so errors are first encapsulated in a `std::function`, and only if an error actually occurs (none of the overloads could be called) is the error message builded.

When the `Xtcl::error::text()` function is called, only short strings should be used so that SSO can be applied to avoid heap allocation.

## README code

```c++
#include <xtcl.h>

#include <array>
#include <cassert>

#include <string>
#include <iostream>

namespace
{
    float clamp(float value, float low, float high)
    {
        assert(low <= high);
        return (value < low) ? low : (value > high) ? high : value;
    }

    struct Vec2
    {
        float x, y;
    };

    Vec2 vec2_new(float x, float y)
    {
        return {x, y};
    }

    float vec2_dot(Vec2 const & a, Vec2 const & b)
    {
        return (a.x * b.x + a.y * b.y);
    }
}

template <> struct Xtcl::Type<Vec2>
{
    static auto name() { return "<vec2>"; }

    using TR = std::array<float, 2>;

    static Xtcl::FromResult<Vec2> from(Tcl_Interp * tcl, Tcl_Obj * obj)
    {
        auto xy = Xtcl::from<TR>(tcl, obj);

        if (not xy)
        {
            return Xtcl::Error::forward(xy.error());
        }

        auto [x, y] = *xy;

        return Vec2 {x, y};
    }

    static Xtcl::ToResult to(Tcl_Interp * tcl, Vec2 const& value)
    {
        return Xtcl::to(tcl, TR {value.x, value.y});
    }
};

int main()
{
    Tcl_Interp * tcl = Tcl_CreateInterp();

    Xtcl::add_function(tcl, "clamp", clamp);

    Xtcl::add_function(tcl, "vec2_new", vec2_new);
    Xtcl::add_function(tcl, "vec2_dot", vec2_dot);

    Xtcl::add_function
    (
        tcl, "ok",
        [] (int) {std::cout << "got an int\n";},
        [] (std::string const &) {std::cout << "got a string\n";}
    );

    Xtcl::add_function
    (
        tcl, "ko",
        [] (std::string const &) {std::cout << "got a string\n";},
        [] (int) {std::cout << "got an int\n";}
    );

    std::string cmd {};
    while (std::cin.good())
    {
        if (cmd.empty()) std::cout << "% ";

        {
            std::string line; std::getline(std::cin, line);
            if (line.empty()) continue;
            if (!cmd.empty()) cmd += '\n';
            cmd += line;
        }

        if (Tcl_CommandComplete(cmd.c_str()))
        {
            int e = Tcl_Eval(tcl, cmd.c_str());
            char const * r = Tcl_GetStringResult(tcl);
            if (r[0] != 0)
            {
                ((e == TCL_ERROR) ? std::cerr : std::cout) << r << std::endl; 
            }
            cmd.clear();
        }
    }

    Tcl_DeleteInterp(tcl);

    return 0;
}
```
