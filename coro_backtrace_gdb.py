# gdb_coro_debugging.py
import gdb
import gdb.printing
from gdb.FrameDecorator import FrameDecorator

import typing
import re

def _load_pointer_at(addr: int):
    return gdb.Value(addr).reinterpret_cast(gdb.lookup_type('void').pointer().pointer()).dereference()

"""
Devirtualized coroutine frame.

Devirtualizes the promise and frame pointer types by inspecting
the destroy function.

Implements `to_string` and `children` to be used by `gdb.printing.PrettyPrinter`.
Base class for `CoroutineHandlePrinter`.
"""
class DevirtualizedCoroFrame:
    def __init__(self, frame_ptr_raw: int, val: gdb.Value | None = None):
        self.val = val
        self.frame_ptr_raw = frame_ptr_raw

        # Get the resume and destroy pointers.
        if frame_ptr_raw == 0:
            self.resume_ptr = None
            self.destroy_ptr = None
            self.promise_ptr = None
            self.frame_ptr = gdb.Value(frame_ptr_raw).reinterpret_cast(gdb.lookup_type("void").pointer())
            return

        # Get the resume and destroy pointers.
        self.resume_ptr = _load_pointer_at(frame_ptr_raw)
        self.destroy_ptr = _load_pointer_at(frame_ptr_raw + 8)

        # Devirtualize the promise and frame pointer types.
        frame_type = gdb.lookup_type("void")
        promise_type = gdb.lookup_type("void")
        self.destroy_func = gdb.block_for_pc(int(self.destroy_ptr))
        if self.destroy_func is not None:
            # Clang uses __coro_frame, GCC uses frame_ptr
            frame_var = gdb.lookup_symbol("__coro_frame", self.destroy_func, gdb.SYMBOL_VAR_DOMAIN)[0]
            if frame_var is None:
                frame_var = gdb.lookup_symbol("frame_ptr", self.destroy_func, gdb.SYMBOL_VAR_DOMAIN)[0]
            if frame_var is not None:
                frame_type = frame_var.type
                # GCC uses a pointer type for frame_ptr
                if frame_type.code == gdb.TYPE_CODE_PTR:
                    frame_type = frame_type.target()
            # Clang uses __promise, GCC uses _Coro_promise
            promise_var = gdb.lookup_symbol("__promise", self.destroy_func, gdb.SYMBOL_VAR_DOMAIN)[0]
            if promise_var is None:
                promise_var = gdb.lookup_symbol("_Coro_promise", self.destroy_func, gdb.SYMBOL_VAR_DOMAIN)[0]
            if promise_var is not None:
                promise_type = promise_var.type.strip_typedefs()
            elif frame_type.code == gdb.TYPE_CODE_STRUCT:
                # GCC embeds _Coro_promise as a field in the frame struct
                for field in frame_type.fields():
                    if field.name == "_Coro_promise":
                        promise_type = field.type.strip_typedefs()
                        break

        # If the type has a template argument, prefer it over the devirtualized type.
        if self.val is not None:
            promise_type_template_arg = self.val.type.template_argument(0)
            if promise_type_template_arg is not None and promise_type_template_arg.code != gdb.TYPE_CODE_VOID:
                promise_type = promise_type_template_arg

        self.promise_ptr = gdb.Value(frame_ptr_raw + 16).reinterpret_cast(promise_type.pointer())
        self.frame_ptr = gdb.Value(frame_ptr_raw).reinterpret_cast(frame_type.pointer())

        # Try to get the suspension point index and look up the exact line entry.
        # Clang uses __coro_index (1 byte), GCC uses _Coro_resume_index (2 bytes)
        self.suspension_point_index = None
        self.is_gcc = False
        if frame_type.code == gdb.TYPE_CODE_STRUCT:
            frame_deref = self.frame_ptr.dereference()
            try:
                self.suspension_point_index = int(frame_deref["__coro_index"])
            except gdb.error:
                try:
                    self.suspension_point_index = int(frame_deref["_Coro_resume_index"])
                    self.is_gcc = True
                except gdb.error:
                    pass
        self.resume_func = gdb.block_for_pc(int(self.resume_ptr))
        self.resume_label = None
        self.await_point_sal = None  # Source and line for await point
        if self.resume_func is not None and self.suspension_point_index is not None:
            if self.is_gcc:
                # GCC: use disassembly scanning to find await point
                self.await_point_sal = self._find_await_point_sal_gcc(frame_type)
            else:
                # Clang: use resume label
                label_name = f"__coro_resume_{self.suspension_point_index}"
                self.resume_label = gdb.lookup_symbol(label_name, self.resume_func, gdb.SYMBOL_LABEL_DOMAIN)[0]
    
    # Cache for resume function -> (coro_index -> sal) mapping
    _await_point_cache = {}
    
    def _find_await_point_sal_gcc(self, frame_type):
        """
        Find the source line for a GCC coroutine await point by scanning disassembly.
        
        GCC's debug info for suspend points often points to the function's closing
        brace. We scan the resume function's disassembly to find where coro_index
        is stored, then scan backwards to find meaningful line info.
        """
        try:
            resume_fn_addr = int(self.resume_ptr)
            
            # Check cache first
            if resume_fn_addr in self._await_point_cache:
                index_to_sal = self._await_point_cache[resume_fn_addr]
                return index_to_sal.get(self.suspension_point_index)
            
            # Get coro_index offset from frame type
            coro_index_offset = None
            for field in frame_type.fields():
                if field.name == "_Coro_resume_index":
                    coro_index_offset = field.bitpos // 8
                    break
            
            if coro_index_offset is None:
                return None
            
            # Build the index -> sal mapping by scanning disassembly
            index_to_sal = self._build_await_point_map_gcc(resume_fn_addr, coro_index_offset)
            self._await_point_cache[resume_fn_addr] = index_to_sal
            
            return index_to_sal.get(self.suspension_point_index)
        except Exception:
            return None
    
    def _build_await_point_map_gcc(self, resume_fn_addr, coro_index_offset):
        """
        Scan the resume function's disassembly to map coro_index values to
        their await point source locations.
        
        Looks for: movw $N, 0xNN(%reg) where 0xNN is the coro_index offset.
        """
        index_to_sal = {}
        
        try:
            arch = gdb.selected_frame().architecture()
            
            # Get function boundaries
            block = self.resume_func
            while block.function is None and block.superblock is not None:
                block = block.superblock
            if block.function is None:
                return index_to_sal
            
            func_start = block.start
            func_end = block.end
            
            # Disassemble the function
            disasm = arch.disassemble(func_start, func_end)
            
            # Find the "end line" (closing brace) - the most common line number
            line_counts = {}
            for instr in disasm:
                sal = gdb.find_pc_line(instr['addr'])
                if sal.line > 0:
                    line_counts[sal.line] = line_counts.get(sal.line, 0) + 1
            
            func_end_line = 0
            if line_counts:
                max_line = max(line_counts.keys())
                max_count_line = max(line_counts, key=line_counts.get)
                # Closing brace is usually near the end with highest count
                if max_count_line >= max_line - 5:
                    func_end_line = max_count_line
                else:
                    func_end_line = max_line
            
            # Scan for movw instructions storing to coro_index offset
            offset_hex = f"0x{coro_index_offset:x}"
            offset_dec = str(coro_index_offset)
            
            for i, instr in enumerate(disasm):
                asm = instr['asm']
                # Look for: movw $N, offset(%reg)
                if asm.startswith('movw') and '$' in asm:
                    parts = asm.split(',', 1)
                    if len(parts) == 2:
                        imm_part = parts[0].replace('movw', '').strip()
                        mem_part = parts[1].strip()
                        
                        # Check if immediate and offset match
                        if imm_part.startswith('$') and (offset_hex in mem_part or f"({offset_dec})" in mem_part or mem_part.startswith(f"{coro_index_offset}(")):
                            try:
                                imm_val = int(imm_part[1:], 0)
                            except ValueError:
                                continue
                            
                            # Scan backwards to find meaningful line info
                            sal = self._find_await_sal_backwards(disasm, i, func_end_line)
                            if sal is not None:
                                index_to_sal[imm_val] = sal
        except Exception:
            pass
        
        return index_to_sal
    
    def _find_await_sal_backwards(self, disasm, store_idx, func_end_line):
        """
        Scan backwards from the coro_index store instruction to find
        an instruction with meaningful line info (not the closing brace).
        """
        try:
            # Start with the store instruction
            best_sal = gdb.find_pc_line(disasm[store_idx]['addr'])
            
            # Scan backwards to find better line info
            for j in range(store_idx - 1, max(0, store_idx - 50), -1):
                sal = gdb.find_pc_line(disasm[j]['addr'])
                if sal.line > 0 and sal.line != func_end_line:
                    return sal
            
            return best_sal
        except Exception:
            return None

    def get_function_name(self):
        if self.destroy_func is None:
            return None
        name = self.destroy_func.function.name
        # Strip the "clone" suffix if it exists.
        if "() [clone " in name:
            name = name[:name.index("() [clone ")]
        return name

    def to_string(self):
        result = "coro(" + str(self.frame_ptr_raw) + ")"
        if self.destroy_func is not None:
            result += ": " + self.get_function_name()
        if self.resume_label is not None:
            result += ", line " + str(self.resume_label.line)
        if self.suspension_point_index is not None:
            result += ", suspension point " + str(self.suspension_point_index)
        return result

    def children(self):
        if self.resume_ptr is None:
            return [
                ("coro_frame", self.frame_ptr),
            ]
        else:
            return [
                ("resume", self.resume_ptr),
                ("destroy", self.destroy_ptr),
                ("promise", self.promise_ptr),
                ("coro_frame", self.frame_ptr)
            ]


# Works for both libc++ and libstdc++, and tmc::work_item alias
libcxx_corohdl_regex = re.compile('^std::__[A-Za-z0-9]+::coroutine_handle<.*>( [&*])?$|^std::coroutine_handle<.*>( [&*])?$|^tmc::work_item( [&*])?$')

def _extract_coro_frame_ptr_from_handle(val: gdb.Value):
    type_name = val.type.strip_typedefs().name
    if type_name is None or libcxx_corohdl_regex.match(type_name) is None:
        raise ValueError("Expected a std::coroutine_handle, got %s" % type_name)

    # We expect the coroutine handle to have a single field, which is the frame pointer.
    # This heuristic works for both libc++ and libstdc++.
    fields = val.type.fields()
    if len(fields) != 1:
        raise ValueError("Expected 1 field, got %d" % len(fields))
    return int(val[fields[0]])


"""
Pretty printer for `std::coroutine_handle<T>`

Works for both libc++ and libstdc++.

It prints the coroutine handle as a struct with the following fields:
- resume: the resume function pointer
- destroy: the destroy function pointer
- promise: the promise pointer
- coro_frame: the coroutine frame pointer

Most of the functionality is implemented in `DevirtualizedCoroFrame`.
"""
class CoroutineHandlePrinter(DevirtualizedCoroFrame):
    def __init__(self, val : gdb.Value):
        # Handle references by dereferencing
        if val.type.code == gdb.TYPE_CODE_REF:
            val = val.referenced_value()
        # Handle pointers
        if val.type.code == gdb.TYPE_CODE_PTR:
            val = val.dereference()
        
        # Check if type has exactly one field (the frame pointer)
        fields = val.type.strip_typedefs().fields()
        if len(fields) != 1:
            raise ValueError("Expected 1 field, got %d" % len(fields))
        frame_ptr_raw = int(val[fields[0]])
        super(CoroutineHandlePrinter, self).__init__(frame_ptr_raw, val)


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("coroutine")
    pp.add_printer('std::coroutine_handle', libcxx_corohdl_regex, CoroutineHandlePrinter)
    return pp

gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer())


"""
Get the coroutine frame pointer from a coroutine handle.

Usage:
```
p *get_coro_frame(coroutine_hdl)
```
"""
class GetCoroFrame(gdb.Function):
    def __init__(self):
        super(GetCoroFrame, self).__init__("get_coro_frame")

    def invoke(self, coroutine_hdl_raw):
        return CoroutineHandlePrinter(coroutine_hdl_raw).frame_ptr

GetCoroFrame()


"""
Get the coroutine frame pointer from a coroutine handle.

Usage:
```
p *get_coro_promise(coroutine_hdl)
```
"""
class GetCoroFrame(gdb.Function):
    def __init__(self):
        super(GetCoroFrame, self).__init__("get_coro_promise")

    def invoke(self, coroutine_hdl_raw):
        return CoroutineHandlePrinter(coroutine_hdl_raw).promise_ptr

GetCoroFrame()


"""
Decorator for coroutine frames.

Used by `CoroutineFrameFilter` to add the coroutine frames to the built-in `bt` command.
"""
class CoroutineFrameDecorator(FrameDecorator):
    def __init__(self, coro_frame: DevirtualizedCoroFrame, inferior_frame: gdb.Frame, is_fork: bool = False):
        super(CoroutineFrameDecorator, self).__init__(inferior_frame)
        self.coro_frame = coro_frame
        self.is_fork = is_fork

    def function(self):
        prefix = "[fork] [async] " if self.is_fork else "[async] "
        func_name = self.coro_frame.get_function_name()
        if func_name is not None:
            return prefix + func_name
        return prefix + "coroutine (coro_frame=" + str(self.coro_frame.frame_ptr_raw) + ")"

    def address(self):
        # Return the resume function address for MI mode
        if self.coro_frame.resume_ptr is not None:
            return int(self.coro_frame.resume_ptr)
        return None

    def filename(self):
        if self.coro_frame.destroy_func is not None:
            return self.coro_frame.destroy_func.function.symtab.filename
        return None

    def line(self):
        # Clang: use resume_label
        if self.coro_frame.resume_label is not None:
            return self.coro_frame.resume_label.line
        # GCC: use await_point_sal from disassembly scanning
        if self.coro_frame.await_point_sal is not None:
            return self.coro_frame.await_point_sal.line
        return None

    def frame_args(self):
        return []

    def frame_locals(self):
        return []


def _get_continuation(promise: gdb.Value, is_gcc: bool) -> tuple[DevirtualizedCoroFrame | None, bool]:
    """
    Returns a tuple of (continuation_frame, is_fork).
    is_fork is True if the continuation is indirected (done_count != 0).
    """
    try:
        # Dereference if promise is a pointer
        if promise.type.code == gdb.TYPE_CODE_PTR:
            promise = promise.dereference()
        
        # TMC stores continuation in promise.customizer.continuation as void*
        customizer = promise["customizer"]
        continuation = customizer["continuation"]
        done_count = customizer["done_count"]
        
        cont_addr = int(continuation)
        if cont_addr == 0:
            return None, False
        
        is_fork = int(done_count) != 0
        
        # If done_count is non-null, continuation is indirect (pointer to handle)
        if is_fork:
            cont_addr = int(_load_pointer_at(cont_addr))
            if cont_addr == 0:
                return None, False
        
        return DevirtualizedCoroFrame(cont_addr), is_fork
    except Exception as e:
        return None, False


def _create_coroutine_frames(coro_frame: DevirtualizedCoroFrame, inferior_frame: gdb.Frame, is_gcc: bool, is_fork: bool = False):
    while coro_frame is not None:
        yield CoroutineFrameDecorator(coro_frame, inferior_frame, is_fork)
        coro_frame, is_fork = _get_continuation(coro_frame.promise_ptr, is_gcc)


"""
Frame filter to add coroutine frames to the built-in `bt` command.
"""
class CppCoroutineFrameFilter():
    def __init__(self):
        self.name = "CppCoroutineFrameFilter"
        self.priority = 50
        self.enabled = True
        # Register this frame filter with the global frame_filters dictionary.
        gdb.frame_filters[self.name] = self

    def filter(self, frame_iter: typing.Iterable[gdb.FrameDecorator]):
        # Note: In GDB/MI mode (used by VS Code), synthetic frames don't have level
        # fields which may cause issues with some frontends. Console `bt` works fine.
        generated_async_frames = False  # Only generate once from topmost coroutine
        for frame in frame_iter:
            yield frame
            if generated_async_frames:
                continue
            try:
                inferior_frame = frame.inferior_frame()
                if inferior_frame is None or not inferior_frame.is_valid():
                    continue
                # Clang uses __promise, GCC uses _Coro_promise
                promise_ptr = None
                is_gcc = False
                try:
                    promise_ptr = inferior_frame.read_var("__promise")
                except Exception:
                    pass
                if promise_ptr is None:
                    try:
                        promise_ptr = inferior_frame.read_var("_Coro_promise")
                        is_gcc = True
                    except Exception:
                        continue
                parent_coro, is_fork = _get_continuation(promise_ptr, is_gcc)
                if parent_coro is not None:
                    yield from _create_coroutine_frames(parent_coro, inferior_frame, is_gcc, is_fork)
                    generated_async_frames = True
            except Exception:
                continue

CppCoroutineFrameFilter()