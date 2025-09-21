#ifndef XTCL_SHELL_H
#define XTCL_SHELL_H

#include <tcl/tcl.h>

#include <expected>
#include <string>
#include <string_view>

#include "xtcl.h"

namespace Xtcl
{
    class Shell
    {
        Tcl_Interp * interp;

    public:

        Shell() : interp(Tcl_CreateInterp()) {}

        ~Shell() {Tcl_DeleteInterp(interp);}

        Shell(Shell const &) = delete;
        Shell(Shell &&     ) = delete;

        Shell & operator = (Shell const &) = delete;
        Shell & operator = (Shell &&     ) = delete;

        Tcl_Interp * tcl() {return interp;}

#if XTCL_COPY_RESULT
        using Result = std::expected<std::string, std::string>;
#else
        using Result = std::expected<std::string_view, std::string_view>;
#endif

        Result eval(char const * cmd)
        {
            int r = Tcl_Eval(tcl(), cmd);
            char const * s = Tcl_GetString(Tcl_GetObjResult(tcl()));

            if (r != TCL_OK)
            {
                return std::unexpected(s);
            }

            return s;
        }

        Result eval(std::string const & cmd)
        {
            return eval(cmd.c_str());
        }

        template <typename... Fs>
        void add_function(char const * name, Fs && ... fs)
        {
            Xtcl::add_function(tcl(), name, std::forward<Fs>(fs)...);
        }

        template <typename... Fs>
        void add_function(std::string const & name, Fs && ... fs)
        {
            return add_function(name.c_str(), std::forward<Fs>(fs)...);
        }
    };
}

#endif
