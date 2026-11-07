// The default verbose terminate handler pulls in the C++ demangler (~25 KB of flash) to
// print a message the device has nowhere to show. Replace it with a plain abort.
#include <cstdlib>

namespace __gnu_cxx
{
void __verbose_terminate_handler()
{
    abort();
}
} // namespace __gnu_cxx
