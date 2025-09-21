# Xtcl

Basic C++ 23 Tcl extension helper.

Not sure exactly why I did this. I was wondering: how feasible/easy/hard would it be is to automatically call (and handle errors nicely) a function expecting a fixed number of typed arguments from an old skool function `(argc, argv[])` style function expecting a variable number of untyped arguments? As many scripting languages use the `(argc, argv[])` paradigm for C extensions, I chose to do a helper for one of them as a practical exercise, and I ended up choosing Tcl because:

- Extending Tcl is super easy.
- For some reason, Tcl has a special place in my ❤️ (althought I haven't used it since - pfuuu... - 20 years?).

# Quick tour

Assume you want to add this basic function to the Tcl interpreter :

```c
float clamp(float value, float low, float high)
{
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

Notice how errors are easy to make in the code above:

- Errors on the hard-coded index when retrieving arguments (I know, I did it because of copy-paste). Could be mitigated with a `*ptr++` coding style, but still.
- Forgetting to check the number of arguments before retrieveing them (again, I know...)

And these are just 3 basic float arguments, things get much worse when lists or dictionaries come into play.

`Xtcl` allows to do the same thing in a single-can't-be-simpler line:

```c++
Xtcl::add_function(tcl, "clamp", clamp);
```

`Xtcl` is not limited to functions, it can be used with almost any callable object:

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

Also, defined procs can be "overloaded" using several functions:

    ```c++
    Xtcl::add_function
    (
        tcl, "multi",
        [] (std::string const & s) {/*...*/},
        [] (int a, int b) {/*...*/},
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
- booleans
- strings
- tuples
- most used containers: vectors, arrays, maps, and sets

| C++ | Tcl |
| --- | --- |
| integrals | `Tcl_WideInt` |
| floating-points | `double` |
| `bool` | boolean (`int`) |
| `std::string`<br>`string_view` | string |
| `std::tuple`<br>`std::vector`<br>`std::array`<br>`std::set`<br>`std::unordered_set` | list |
| `std::map`<br>`std::unordered_map` | dict |

### Integers overflows

If the `XTCL_OVERFLOW_ERROR` definition is enabled (default), an error is raised when a Tcl integer cannot fit in the destination type. Otherwise, integers are simply truncated in the same way as in the C language.

### User type support

If a type is not already supported, it is possible to support it by specializing the `Xtcl::Type` templated structure:

```c++
struct Usr
{
    int a, b;
    float c;
};

template <> struct Xtcl::Type<Usr>
{
    // mandatory
    static auto name() {return "<usr>"sv;}

    // if used as an argument
    static Xtcl::FromResult<Usr> from(Tcl_Interp * tcl, Tcl_Obj * obj)
    {
        auto fields = Xtcl::from<std::tuple<int, int, float>>(tcl, obj);
        if (not fields)
        {
            return Xtcl::Error::forward(fields.error());
        }

        auto [a, b, c] = *fields;

        return Usr {a, b, c};
    }

    // if used as a returned value
    static Xtcl::ToResult to(Tcl_Interp * tcl, Usr const & value)
    {
        return Xtcl::to(tcl, std::make_tuple(value.a, value.b, value.c));
    }
};
```

### Qualification and references

As seen in the code snipets above, qualifications and references are supported, not just plain values. Mutable references are also supported, but it rarely makes sense since values are disposed right after the call.

### C strings

If the `XTCL_SUPPORT_CSTRING` definition is enabled (default), `char const *` values are assumed to be null terminating C strings.

### Pointers

If the `XTCL_SUPPORT_POINTER` definition is enabled (default), pointers are supported. The pointee is assumed to be a single value, not an array (except for `char const *` if `XTCL_SUPPORT_CSTRING` definition is enabled, see above).

## Overloads

The way "overloads" are handled is quite dumb: functions are checked in the order they are added, and the first one whose arguments match is the one that is called. This means that the order functions are added is important:

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
    [] (std::string const &) {std::cout << "got a string\n";},
    [] (int) {std::cout << "got an int\n";}
);
```

In the `ko` proc, the `int` flavored function will never be called since the `std::string` one is always valid.

## Error handling

Because of the way "overloads" are designed, it is expected that some functions calls fail, and this is why error handling is somewhat convoluted. Error messages are costly in terms of CPU and memory, and so errors are first encapsulated in a `std::function`, and only if an error actually occurs (none of the overloads could be called) is the error message builded. When the `Xtcl::error::text()` function is used, only small strings should be passed so that SSO can apply to avoid heap allocation. In the same spirit, lambdas passed to the `Xtcl::error::generic()` function should have a small context.
