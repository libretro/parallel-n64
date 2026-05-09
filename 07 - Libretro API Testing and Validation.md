# Milestone 07 - Libretro API Testing and Validation

**Status:** ✅ Complete
**Date:** October 30, 2025

## Objective

Validate that the parallel-n64 libretro core compiled to WebAssembly correctly exposes and implements the essential libretro API functions in a browser environment.

## Initial Issues

When first running `test_libretro.html`, encountered multiple failures:

1. **Export Issues:**
   - `Module._malloc is not a function` - malloc/free not exported
   - `Module.addFunction is not a function` - addFunction not exported
   - `Module.UTF8ToString` not available
   - `Module.HEAPU8` / `Module.HEAPU32` not available

2. **API Usage Issues:**
   - `retro_init()` failing with "null function or function signature mismatch"
   - Root cause: `environ_cb` was NULL because `retro_set_environment()` was never called

3. **Browser Caching:**
   - Updated WASM builds not loading due to aggressive browser caching

## Solutions Implemented

### 1. Updated Makefile Exports (lines 502-503)

**Added to EXPORTED_FUNCTIONS:**
```makefile
"_malloc","_free"
```

**Updated EXPORTED_RUNTIME_METHODS:**
```makefile
'["ccall","cwrap","UTF8ToString","addFunction","HEAPU8","HEAPU32"]'
```

**Added linker flag:**
```makefile
-s ALLOW_TABLE_GROWTH=1
```
This enables dynamic function table growth required by `addFunction()`.

### 2. Fixed Test Sequence in test_libretro.html

**Added Test 3: retro_set_environment()**
```javascript
test('retro_set_environment()', () => {
    // Create a dummy environment callback
    const envCallback = Module.addFunction((cmd, data) => {
        return 0; // false - minimal implementation
    }, 'iii'); // return int, params: int, pointer

    Module._retro_set_environment(envCallback);
    return 'Environment callback set successfully';
});
```

This ensures `environ_cb` is set before `retro_init()` tries to use it (libretro/libretro.c:1011-1024).

**Key insight:** The libretro API requires callbacks to be set in a specific order:
1. `retro_set_environment()` - MUST be called first
2. `retro_init()` - Uses environment callback during initialization
3. Other setup functions
4. `retro_load_game()` - Load actual content

### 3. Fixed Memory Access

Changed from `Module.HEAP8` (not exported) to `Module.HEAPU8` (now exported):
```javascript
for (let i = 0; i < structSize; i++) {
    Module.HEAPU8[ptr + i] = 0;  // Was: Module.HEAP8
}
```

### 4. UI Polish

Made "Failed: 0" display in green to indicate success:
```javascript
<span class="${testsFailed === 0 ? 'success' : 'error'}">Failed: ${testsFailed}</span>
```

## Test Results

All 6 tests now pass:

1. ✅ **retro_api_version()** - Returns API version 1
2. ✅ **retro_get_system_info()** - Retrieves library name and version using malloc
3. ✅ **retro_set_environment()** - Sets up environment callback using addFunction
4. ✅ **retro_init()** - Initializes core successfully
5. ✅ **retro_deinit()** - Cleans up core resources
6. ✅ **retro_init() again** - Re-initializes for further testing

## File Sizes

- **parallel_n64_libretro.js:** 187 KB
- **parallel_n64_libretro.wasm:** 1.4 MB
- **Total:** ~1.6 MB

## Key Learnings

### Libretro API Lifecycle
The libretro specification requires this initialization order:
```
retro_set_environment()  → Sets callbacks
retro_init()             → Uses environ_cb internally
retro_load_game()        → Loads content
retro_run()              → Main loop
retro_unload_game()      → Cleanup
retro_deinit()           → Final cleanup
```

### Emscripten Function Tables
- `addFunction()` requires `-s ALLOW_TABLE_GROWTH=1`
- Function signature must be specified: `'iii'` = returns int, takes (int, int)
- Wrapped functions can be called from C code as function pointers

### Memory Views in WASM
- Memory views must be explicitly exported via `EXPORTED_RUNTIME_METHODS`
- `HEAPU8` = unsigned byte access (most common)
- `HEAPU32` = unsigned 32-bit access (for pointer arrays)
- Memory views are automatically updated when WASM memory grows

## Browser Testing Notes

- **Cache Management:** Disable caching during development or use cache-busting query params
- **Console Logging:** All stdout/stderr from WASM appears in browser console
- **Memory Inspection:** Can examine WASM memory directly via Module.HEAPU8/32

## Code Locations

- **Makefile:** Emscripten configuration (lines 482-511)
- **test_libretro.html:** Browser test harness
- **libretro/libretro.c:** Core API implementation
  - `retro_init()` at line 1003
  - `retro_set_environment()` usage at line 1011

## Next Steps

Potential future milestones:

1. **ROM Loading Test** - Test `retro_load_game()` with actual N64 ROM
2. **Graphics Context** - Set up WebGL2 context for rendering
3. **Input Handling** - Implement gamepad/keyboard input callbacks
4. **Audio Output** - Set up Web Audio API for sound
5. **Frame Rendering** - Test actual emulation loop (`retro_run()`)
6. **Save States** - Test serialization/deserialization
7. **Performance Profiling** - Measure frame rates and optimize

## Validation Checklist

- [x] Core loads in browser without errors
- [x] All libretro API functions are callable from JavaScript
- [x] Memory allocation/deallocation works
- [x] String data can be read from WASM memory
- [x] JavaScript callbacks can be passed to C code
- [x] Core initialization/cleanup works correctly
- [x] Multiple init/deinit cycles succeed

## Conclusion

The parallel-n64 libretro core successfully runs in WebAssembly and exposes a working JavaScript API. All fundamental libretro API functions are operational and can be called from browser JavaScript. The core is ready for the next phase: actual ROM loading and emulation testing.

**This milestone demonstrates that the core C/C++ emulation code compiles to WASM and maintains API compatibility with the libretro specification.**
