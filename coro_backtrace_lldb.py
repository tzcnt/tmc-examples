# coro_backtrace_lldb.py
# LLDB ScriptedFrameProvider for C++20 coroutine stack unwinding
import re
import lldb
from lldb.plugins.scripted_frame_provider import ScriptedFrameProvider
from lldb.plugins.scripted_process import ScriptedFrame

# Enable to print debug messages
DEBUG = False

def debug_log(msg):
    if DEBUG:
        print(f"[coro-provider] {msg}")


# Compiler-internal coroutine-frame fields that are not user-visible source locals.
_INTERNAL_CORO_FIELDS = frozenset({
    "__resume_fn", "__destroy_fn", "__promise", "__coro_index",
    "_Coro_resume_fn", "_Coro_destroy_fn", "_Coro_promise", "_Coro_resume_index",
    "_Coro_self_handle", "_Coro_frame_size", "_Coro_frame_ptr",
})

# Clang names anonymous await/suspend spill temporaries with struct_/class_/union_
# prefixes and a numeric suffix; these aren't source-level locals.
_SPILL_TEMP_RE = re.compile(r"^(struct|class|union)_.*_\d+$")


def _is_user_coro_local(name):
    return bool(name) and name not in _INTERNAL_CORO_FIELDS and not _SPILL_TEMP_RE.match(name)


class CoroFrame(ScriptedFrame):
    """A synthetic frame representing a coroutine continuation."""
    
    def __init__(self, thread, args, frame_id, pc, is_fork, coro_frame_addr=None, coro_type=None):
        # Don't call super().__init__ as it requires specific thread types
        try:
            self.thread = thread
            self.target = thread.GetProcess().GetTarget() if thread else None
            self.arch = self.target.triple.split("-")[0] if self.target and self.target.triple else None
            self.args = args
            self._id = frame_id
            self._pc = pc
            self._is_fork = is_fork
            # Coroutine frame pointer + devirtualized struct type, used to resolve the
            # suspended coroutine's spilled locals (see get_variables). These have no
            # register/frame context, so locals come from the frame struct in memory.
            self._coro_frame_addr = coro_frame_addr
            self._coro_type = coro_type
            self.register_info = None
            self.register_ctx = {}
            self.variables = []
        except Exception as e:
            debug_log(f"Error in CoroFrame.__init__: {e}")
            self.thread = None
            self.target = None
            self.arch = None
            self.args = None
            self._id = frame_id
            self._pc = pc
            self._is_fork = is_fork
            self._coro_frame_addr = coro_frame_addr
            self._coro_type = coro_type
            self.register_info = None
            self.register_ctx = {}
            self.variables = []
    
    def get_id(self):
        return self._id
    
    def get_pc(self):
        return self._pc
    
    def get_symbol_context(self):
        """Get the symbol context for this frame, including correct line entry."""
        try:
            if self.target is None:
                return None
            addr = lldb.SBAddress(self._pc, self.target)
            return addr.GetSymbolContext(
                lldb.eSymbolContextFunction | 
                lldb.eSymbolContextSymbol | 
                lldb.eSymbolContextLineEntry |
                lldb.eSymbolContextCompUnit
            )
        except Exception as e:
            debug_log(f"Error in get_symbol_context: {e}")
            return None
    
    def get_function_name(self):
        try:
            if self.target is None:
                return "[fork] <unknown>" if self._is_fork else "<unknown>"
            # Look up the symbol at this PC
            addr = lldb.SBAddress(self._pc, self.target)
            symbol = addr.GetSymbol()
            if symbol.IsValid():
                name = symbol.GetName()
                if self._is_fork:
                    return f"[fork] {name}"
                return name
        except Exception as e:
            debug_log(f"Error in get_function_name: {e}")
        if self._is_fork:
            return "[fork] <unknown>"
        return "<unknown>"
    
    def get_display_function_name(self):
        return self.get_function_name()
    
    def is_artificial(self):
        return True

    def get_register_context(self):
        return None

    def get_variables(self, filters):
        """Provide the suspended coroutine's spilled locals.

        A suspended coroutine has no register/frame context, so its locals cannot be
        read the usual way. They live as fields of the coroutine frame struct in memory;
        we materialize that struct at the frame pointer and expose each user field as a
        local (filtering out compiler-internal bookkeeping and spill temporaries).

        NOTE: As of LLDB 22.x this hook is defined in the ScriptedFrame Python API but
        LLDB's C++ side does not yet invoke it for variable display (`frame variable`,
        SBFrame.GetVariables, or the DAP Variables pane) - see LLVM PR #178575, which is
        still in review. This implementation is therefore correct but dormant; it will
        begin populating the Variables pane automatically once that support ships. The
        GDB path (syntheticFrameLocalsExpression) provides this today.
        """
        variables = lldb.SBValueList()
        try:
            if self.target is None or self._coro_frame_addr is None or self._coro_type is None:
                return variables
            if not self._coro_type.IsValid():
                return variables
            addr = lldb.SBAddress(self._coro_frame_addr, self.target)
            struct = self.target.CreateValueFromAddress("__coro_frame", addr, self._coro_type)
            if not struct.IsValid():
                return variables
            for i in range(struct.GetNumChildren()):
                child = struct.GetChildAtIndex(i)
                if not child.IsValid():
                    continue
                if _is_user_coro_local(child.GetName()):
                    variables.Append(child)
        except Exception as e:
            debug_log(f"Error in get_variables: {e}")
        return variables


class CoroFrameProvider(ScriptedFrameProvider):
    # Coroutine frame layout (64-bit):
    # offset 0: resume_fn pointer
    # offset 8: destroy_fn pointer  
    # offset 16: promise starts
    #   promise layout (task_promise):
    #     offset 0: customizer.continuation (void*)
    #     offset 8: customizer.continuation_executor (void*)
    #     offset 16: customizer.done_count (void*)
    #     offset 24: customizer.flags (size_t)
    
    def __init__(self, input_frames, args):
        debug_log("CoroFrameProvider.__init__ called")
        super().__init__(input_frames, args)
        self._frames = None
        self._args = args

    @staticmethod
    def applies_to_thread(thread):
        debug_log(f"applies_to_thread called for thread {thread.GetIndexID() if thread else 'None'}")
        return True
    
    @staticmethod
    def get_description():
        return "C++20 coroutine async stack unwinding"

    def _build_frames(self):
        """
        Build the complete frame list by interleaving input frames with
        async coroutine frames.
        
        Only generates synthetic frames from the topmost (active) coroutine frame
        to avoid duplicating the async call chain for each coroutine frame in
        the native backtrace.
        """
        if self._frames is not None:
            return

        self._frames = []
        input_idx = 0
        generated_synthetic = False  # Only generate synthetic frames once

        try:
            while True:
                # Get the next input frame
                input_frame = self.input_frames.GetFrameAtIndex(input_idx)
                if not input_frame.IsValid():
                    break

                # Add the input frame (store its index so we can return it)
                self._frames.append({"input_idx": input_idx})

                # Only generate synthetic frames from the first coroutine frame
                # to avoid duplicating the async call chain
                if not generated_synthetic:
                    # Check if this frame has a promise variable (indicates a coroutine)
                    # Clang uses __promise, GCC uses _Coro_promise
                    try:
                        promise_var = input_frame.GetValueForVariablePath("__promise")
                        is_gcc = False
                        if not promise_var.IsValid():
                            promise_var = input_frame.GetValueForVariablePath("_Coro_promise")
                            is_gcc = True
                        if promise_var.IsValid():
                            # Get the customizer from the promise
                            customizer = promise_var.GetChildMemberWithName("customizer")
                            if customizer.IsValid():
                                async_frames = self._get_coro_frames(customizer, is_gcc)
                                self._frames.extend(async_frames)
                                generated_synthetic = True
                    except Exception as e:
                        debug_log(f"Error processing frame {input_idx}: {e}")

                input_idx += 1
        except Exception as e:
            debug_log(f"Error building frames: {e}")

    # Cache for coro_index -> await point PC mapping per function
    _await_point_cache = {}

    def _get_await_point_pc(self, resume_fn, coro_index, is_gcc):
        """
        Find the PC for an await point given the resume function and coro_index.
        
        Scans the resume function's disassembly to find where coro_index is
        written before each suspension point. The pattern is:
            movb $N, offset(%reg)  ; Clang
            movw $N, offset(%reg)  ; GCC
        where N is the index value being set before suspending.
        
        The address of this instruction indicates where the coroutine was
        about to suspend, which gives us the correct source line.
        """
        try:
            if coro_index is None or coro_index == 0:
                return resume_fn
            
            # Check cache first (include is_gcc in cache key)
            cache_key = (resume_fn, is_gcc)
            if cache_key in self._await_point_cache:
                index_to_pc = self._await_point_cache[cache_key]
                return index_to_pc.get(coro_index, resume_fn)
            
            addr = lldb.SBAddress(resume_fn, self.target)
            func = addr.GetFunction()
            if not func.IsValid():
                return resume_fn
            
            # Build the index->PC mapping by scanning disassembly
            index_to_pc = self._build_await_point_map(func, is_gcc)
            self._await_point_cache[cache_key] = index_to_pc
            
            return index_to_pc.get(coro_index, resume_fn)
        except Exception as e:
            debug_log(f"Error in _get_await_point_pc: {e}")
            return resume_fn

    def _build_await_point_map(self, func, is_gcc):
        """
        Scan the function's disassembly to find where coro_index is stored.
        
        On x86-64, the pattern before suspension is:
            movb $N, 0xNN(%rax)  ; Clang: store __coro_index = N (1 byte)
            movw $N, 0xNN(%rax)  ; GCC: store _Coro_resume_index = N (2 bytes)
        
        Returns a dict mapping coro_index -> PC of the store instruction.
        """
        index_to_pc = {}
        
        try:
            instrs = func.GetInstructions(self.target)
            if not instrs.IsValid():
                return index_to_pc
            
            # Get the offset of coro_index within the coroutine frame
            coro_index_offset = self._get_coro_index_offset(func, is_gcc)
            if coro_index_offset is None:
                return index_to_pc
            
            # GCC needs special handling for line info
            func_end_line = 0
            if is_gcc:
                func_end_line = self._get_function_end_line_gcc(func, instrs)
            
            # Clang uses movb, GCC uses movw
            target_mnemonic = "movw" if is_gcc else "movb"
            
            for i in range(instrs.GetSize()):
                instr = instrs.GetInstructionAtIndex(i)
                mnemonic = instr.GetMnemonic(self.target)
                operands = instr.GetOperands(self.target)
                
                if mnemonic == target_mnemonic:
                    # Parse operands like "$0x1, 0x90(%rax)"
                    parts = operands.split(",")
                    if len(parts) == 2:
                        imm_part = parts[0].strip()
                        mem_part = parts[1].strip()
                        
                        # Check if first operand is an immediate
                        if imm_part.startswith("$"):
                            try:
                                imm_val = int(imm_part[1:], 0)
                            except ValueError:
                                continue
                            
                            # Check if second operand references the coro_index offset
                            # Format: 0xNN(%reg) or NN(%reg)
                            offset_hex = f"0x{coro_index_offset:x}"
                            offset_dec = str(coro_index_offset)
                            if offset_hex in mem_part or offset_dec in mem_part:
                                if is_gcc:
                                    # GCC: scan backwards to find meaningful line info
                                    pc = self._find_await_point_pc_gcc(instrs, i, func_end_line)
                                else:
                                    # Clang: use the instruction's PC directly
                                    pc = instr.GetAddress().GetLoadAddress(self.target)
                                index_to_pc[imm_val] = pc
        except Exception as e:
            debug_log(f"Error in _build_await_point_map: {e}")
        
        return index_to_pc

    def _get_function_end_line_gcc(self, func, instrs):
        """
        Get the closing brace line number for a GCC coroutine .actor function.
        
        GCC's debug info often points suspend-related code to the original
        function's closing brace. We detect this by finding lines that appear
        very frequently (the closing brace is used for all suspend points).
        """
        try:
            # Count occurrences of each line number
            line_counts = {}
            for i in range(instrs.GetSize()):
                le = instrs.GetInstructionAtIndex(i).GetAddress().GetLineEntry()
                if le.IsValid():
                    line = le.GetLine()
                    if line > 0:
                        line_counts[line] = line_counts.get(line, 0) + 1
            
            if not line_counts:
                return 0
            
            # The closing brace line typically has the most references
            # because all suspend points point to it
            max_line = max(line_counts.keys())
            max_count_line = max(line_counts, key=line_counts.get)
            
            # Use the line with max count if it's near the maximum line number
            # (closing brace is usually near the end)
            if max_count_line >= max_line - 5:
                return max_count_line
            
            return max_line
        except Exception:
            pass
        return 0

    def _find_await_point_pc_gcc(self, instrs, store_idx, func_end_line):
        """
        Find the best PC for an await point by scanning backwards from the
        coro_index store instruction (GCC-specific).
        
        GCC associates the store instruction with the function's closing brace,
        but the actual co_await line is in earlier instructions. We scan backwards
        to find an instruction with meaningful line info.
        """
        try:
            # Start with the store instruction's PC
            store_instr = instrs.GetInstructionAtIndex(store_idx)
            best_pc = store_instr.GetAddress().GetLoadAddress(self.target)
            
            # Scan backwards to find better line info
            # Look for the most recent instruction with a different line number
            for j in range(store_idx - 1, max(0, store_idx - 50), -1):
                prev_instr = instrs.GetInstructionAtIndex(j)
                prev_line_entry = prev_instr.GetAddress().GetLineEntry()
                if prev_line_entry.IsValid():
                    prev_line = prev_line_entry.GetLine()
                    # Found an instruction with a real source line (not the end line)
                    if prev_line != func_end_line and prev_line != 0:
                        return prev_instr.GetAddress().GetLoadAddress(self.target)
            
            return best_pc
        except Exception as e:
            debug_log(f"Error in _find_await_point_pc_gcc: {e}")
            return instrs.GetInstructionAtIndex(store_idx).GetAddress().GetLoadAddress(self.target)

    def _get_coro_frame_type(self, resume_fn, is_gcc):
        """
        Return the devirtualized coroutine frame struct SBType for a coroutine, looked up
        from the `__coro_frame` (Clang) / `frame_ptr` (GCC) local of its resume function.
        """
        try:
            addr = lldb.SBAddress(resume_fn, self.target)
            func = addr.GetFunction()
            if not func.IsValid():
                return None
            block = func.GetBlock()
            if not block.IsValid():
                return None
            vars = block.GetVariables(self.target, True, True, True)
            if not vars.IsValid():
                return None
            frame_var_name = "frame_ptr" if is_gcc else "__coro_frame"
            for i in range(vars.GetSize()):
                var = vars.GetValueAtIndex(i)
                if var.GetName() == frame_var_name:
                    coro_type = var.GetType()
                    # GCC uses a pointer type; Clang uses the struct type directly.
                    if is_gcc and coro_type.IsPointerType():
                        coro_type = coro_type.GetPointeeType()
                    return coro_type if coro_type.IsValid() else None
        except Exception as e:
            debug_log(f"Error in _get_coro_frame_type: {e}")
        return None

    def _get_coro_index_offset(self, func, is_gcc):
        """
        Get the byte offset of the coro index within the coroutine frame type.
        """
        try:
            block = func.GetBlock()
            if not block.IsValid():
                return None
            
            # Get variables from the block
            vars = block.GetVariables(self.target, True, True, True)
            if not vars.IsValid():
                return None
            
            # Find coroutine frame variable
            frame_var_name = "frame_ptr" if is_gcc else "__coro_frame"
            coro_frame_var = None
            for i in range(vars.GetSize()):
                var = vars.GetValueAtIndex(i)
                if var.GetName() == frame_var_name:
                    coro_frame_var = var
                    break
            
            if not coro_frame_var:
                return None
            
            coro_type = coro_frame_var.GetType()
            # GCC uses a pointer type
            if is_gcc and coro_type.IsPointerType():
                coro_type = coro_type.GetPointeeType()
            if not coro_type.IsValid():
                return None
            
            # Find coro index field
            index_field_name = "_Coro_resume_index" if is_gcc else "__coro_index"
            for j in range(coro_type.GetNumberOfFields()):
                field = coro_type.GetFieldAtIndex(j)
                if field.GetName() == index_field_name:
                    return field.GetOffsetInBytes()
        except Exception as e:
            debug_log(f"Error in _get_coro_index_offset: {e}")
        
        return None

    def _get_coro_index(self, cont_addr, resume_fn, is_gcc):
        """
        Read coro index from a coroutine frame by looking up the type info.
        Clang uses __coro_index (1 byte), GCC uses _Coro_resume_index (2 bytes).
        """
        try:
            addr = lldb.SBAddress(resume_fn, self.target)
            func = addr.GetFunction()
            if not func.IsValid():
                return None
            
            block = func.GetBlock()
            if not block.IsValid():
                return None
            
            # Get variables from the block
            vars = block.GetVariables(self.target, True, True, True)
            if not vars.IsValid():
                return None
            
            # Find coroutine frame variable
            frame_var_name = "frame_ptr" if is_gcc else "__coro_frame"
            coro_frame_var = None
            for i in range(vars.GetSize()):
                var = vars.GetValueAtIndex(i)
                if var.GetName() == frame_var_name:
                    coro_frame_var = var
                    break
            
            if not coro_frame_var:
                return None
            
            coro_type = coro_frame_var.GetType()
            # GCC uses a pointer type
            if is_gcc and coro_type.IsPointerType():
                coro_type = coro_type.GetPointeeType()
            if not coro_type.IsValid():
                return None
            
            # Find coro index field
            index_field_name = "_Coro_resume_index" if is_gcc else "__coro_index"
            coro_index_field = None
            for i in range(coro_type.GetNumberOfFields()):
                field = coro_type.GetFieldAtIndex(i)
                if field.GetName() == index_field_name:
                    coro_index_field = field
                    break
            
            if not coro_index_field:
                return None
            
            offset = coro_index_field.GetOffsetInBytes()
            # GCC uses 2 bytes, Clang uses 1 byte
            field_size = 2 if is_gcc else 1
            error = lldb.SBError()
            coro_index = self.process.ReadUnsignedFromMemory(cont_addr + offset, field_size, error)
            if error.Fail():
                return None
            
            return coro_index
        except Exception as e:
            debug_log(f"Error in _get_coro_index: {e}")
            return None

    def _get_coro_frames(self, customizer, is_gcc):
        """
        Walk the coroutine continuation chain starting from the customizer.
        Returns a list of CoroFrame objects.
        
        is_gcc: True if compiled with GCC (_Coro_promise), False for Clang (__promise)
        """
        frames = []
        
        try:
            ptr_size = self.target.GetAddressByteSize()
            if ptr_size == 0:
                return frames
            
            # Offsets within coroutine frame (after resume/destroy pointers)
            PROMISE_OFFSET = 2 * ptr_size
            # Offsets within promise/customizer
            CONTINUATION_OFFSET = 0
            DONE_COUNT_OFFSET = 2 * ptr_size
            
            # Check if done_count is null to determine indirection level
            done_count = customizer.GetChildMemberWithName("done_count")
            if not done_count.IsValid():
                return frames
            
            done_count_val = done_count.GetValueAsUnsigned(0)
            
            continuation = customizer.GetChildMemberWithName("continuation")
            if not continuation.IsValid():
                return frames
            
            cont_addr = continuation.GetValueAsUnsigned(0)
            if cont_addr == 0:
                return frames
            
            # If done_count is non-null, continuation is indirect (pointer to handle)
            if done_count_val != 0:
                error = lldb.SBError()
                cont_addr = self.process.ReadPointerFromMemory(cont_addr, error)
                if error.Fail() or cont_addr == 0:
                    return frames
            
            # Walk the continuation chain
            # Track the current done_count to determine if this link is a fork
            current_done_count = done_count_val
            visited = set()
            max_depth = 1000  # Prevent infinite loops
            
            while cont_addr != 0 and cont_addr not in visited and len(frames) < max_depth:
                visited.add(cont_addr)
                
                error = lldb.SBError()
                
                # The coroutine frame starts with the resume function pointer
                resume_fn = self.process.ReadPointerFromMemory(cont_addr, error)
                if error.Fail() or resume_fn == 0:
                    break
                
                # Try to find the await point PC using coro_index
                pc = resume_fn  # Default to resume function start
                coro_index = self._get_coro_index(cont_addr, resume_fn, is_gcc)
                if coro_index is not None:
                    pc = self._get_await_point_pc(resume_fn, coro_index, is_gcc)
                
                # Create a CoroFrame for this continuation. Pass the coroutine frame
                # pointer (cont_addr) and its devirtualized struct type so the frame can
                # resolve the suspended coroutine's spilled locals (get_variables).
                is_fork = current_done_count != 0
                frame_id = len(self._frames) + len(frames)
                coro_type = self._get_coro_frame_type(resume_fn, is_gcc)
                coro_frame = CoroFrame(
                    self.thread, self._args, frame_id, pc, is_fork,
                    coro_frame_addr=cont_addr, coro_type=coro_type
                )
                frames.append({"coro_frame": coro_frame})
                
                # Read the next continuation from this coroutine's promise
                # Promise is at cont_addr + PROMISE_OFFSET
                # Continuation is at promise + CONTINUATION_OFFSET
                promise_addr = cont_addr + PROMISE_OFFSET
                next_cont_addr = self.process.ReadPointerFromMemory(
                    promise_addr + CONTINUATION_OFFSET, error
                )
                if error.Fail():
                    break
                
                # Read done_count to check indirection
                next_done_count = self.process.ReadPointerFromMemory(
                    promise_addr + DONE_COUNT_OFFSET, error
                )
                if error.Fail():
                    break
                
                # If done_count is non-null, dereference continuation
                if next_done_count != 0 and next_cont_addr != 0:
                    next_cont_addr = self.process.ReadPointerFromMemory(next_cont_addr, error)
                    if error.Fail():
                        break
                
                current_done_count = next_done_count
                cont_addr = next_cont_addr
        except Exception as e:
            debug_log(f"Error in _get_coro_frames: {e}")
        
        return frames

    def get_frame_at_index(self, index):
        try:
            self._build_frames()

            if self._frames is None or index >= len(self._frames):
                return None

            frame_info = self._frames[index]

            # If it's an input frame, return its index to reuse it
            if "input_idx" in frame_info:
                return frame_info["input_idx"]

            # Return the CoroFrame object
            return frame_info.get("coro_frame")
        except Exception as e:
            debug_log(f"Error in get_frame_at_index({index}): {e}")
            return None


def _get_lldb_major_version():
    """Extract major version number from LLDB version string."""
    try:
        version_str = lldb.SBDebugger.GetVersionString()
        # Version string format: "lldb version X.Y.Z" or similar
        import re
        match = re.search(r'(\d+)\.', version_str)
        if match:
            return int(match.group(1))
    except Exception:
        pass
    return 0


def __lldb_init_module(debugger, internal_dict):
    major_version = _get_lldb_major_version()
    if major_version < 22:
        print(
            f"Coroutine frame provider requires LLDB 22+. "
            f"Current version: {major_version}. Skipping registration."
        )
        return
    
    debugger.HandleCommand(
        f"target frame-provider register -C {__name__}.CoroFrameProvider"
    )
    print(
        "Coroutine frame provider registered. "
        "Async coroutine frames will appear in backtraces."
    )


if __name__ == "__main__":
    print(
        "This script should be loaded from LLDB using "
        "`command script import <filename>`"
    )
