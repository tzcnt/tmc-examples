file ./build/clang-linux-debug/fib
source coro_backtrace_gdb.py
break examples/fib.cpp:47
run
bt
