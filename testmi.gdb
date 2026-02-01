file ./build/clang-linux-debug/fib
source coro_backtrace_gdb.py
interpreter-exec mi "-enable-frame-filters"
break examples/fib.cpp:47
run
interpreter-exec mi "-stack-list-frames"
