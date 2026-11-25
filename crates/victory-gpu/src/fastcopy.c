// A copy into memory whose pages the kernel has not been asked about.
//
// The title's memory is not ours: a page of it may be decommitted at any
// moment (its streaming code unmaps as it goes), and reading it faults. The
// kernel's own copy (ReadProcessMemory) catches that per page and costs
// about 1 GB/s. A plain memcpy runs at 8-10 GB/s but kills the process on the
// first page that is not there.
//
// This takes the fault instead: the handler takes the access violation, the
// copy is abandoned, and the caller falls back to the kernel's copy. What
// `dst` holds after a fault is partly written, which the caller expects (the
// destination is left as it is when a page is missing).

#include <windows.h>
#include <string.h>

__declspec(noinline) int victory_fast_copy(void *dst, const void *src, size_t len) {
    __try {
        memcpy(dst, src, len);
        return 1;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                    ? EXCEPTION_EXECUTE_HANDLER
                    : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
}
