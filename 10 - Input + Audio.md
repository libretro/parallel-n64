# Milestone 10 - Input + Audio

**Status:** 🔄 Pending
**Date:** October 30, 2025

## Objective

Implement real input handling (keyboard and gamepad) and audio output (Web Audio API) to create a fully interactive N64 emulator running in the browser. Replace stub implementations from Milestone 09 with functional callbacks and establish a continuous emulation loop.

## Prerequisites

- ✅ Milestone 09 complete - First frame rendered successfully
- ✅ WebGL2 rendering working
- ✅ ROM loaded and running
- ✅ Modern browser with Web Audio API and Gamepad API support

## Implementation Steps

### Part A: Input Implementation

#### 1. N64 Controller Button Mapping

N64 controller layout (from libretro API):
```javascript
const N64_BUTTONS = {
    RETRO_DEVICE_ID_JOYPAD_A: 8,       // A button
    RETRO_DEVICE_ID_JOYPAD_B: 0,       // B button
    RETRO_DEVICE_ID_JOYPAD_START: 3,   // Start
    RETRO_DEVICE_ID_JOYPAD_UP: 4,      // D-Pad Up
    RETRO_DEVICE_ID_JOYPAD_DOWN: 5,    // D-Pad Down
    RETRO_DEVICE_ID_JOYPAD_LEFT: 6,    // D-Pad Left
    RETRO_DEVICE_ID_JOYPAD_RIGHT: 7,   // D-Pad Right
    RETRO_DEVICE_ID_JOYPAD_L: 10,      // L trigger
    RETRO_DEVICE_ID_JOYPAD_R: 11,      // R trigger
    RETRO_DEVICE_ID_JOYPAD_L2: 12,     // Z trigger (mapped to L2)
    RETRO_DEVICE_ID_JOYPAD_X: 1,       // C-Up (mapped to X)
    RETRO_DEVICE_ID_JOYPAD_Y: 14,      // C-Down (mapped to Y)
    RETRO_DEVICE_ID_JOYPAD_L3: 15,     // C-Left (mapped to L3)
    RETRO_DEVICE_ID_JOYPAD_R3: 16,     // C-Right (mapped to R3)
    // Analog stick via RETRO_DEVICE_ANALOG
};
```

#### 2. Keyboard Input State

```javascript
const inputState = {
    buttons: new Set(),  // Pressed buttons
    analogX: 0,          // -32768 to 32767
    analogY: 0           // -32768 to 32767
};

// Keyboard mappings
const keyMap = {
    'KeyX': 8,      // A
    'KeyZ': 0,      // B
    'Enter': 3,     // Start
    'ArrowUp': 4,   // D-Up
    'ArrowDown': 5, // D-Down
    'ArrowLeft': 6, // D-Left
    'ArrowRight': 7,// D-Right
    'KeyA': 10,     // L
    'KeyS': 11,     // R
    'KeyQ': 12,     // Z
    'KeyI': 1,      // C-Up
    'KeyK': 14,     // C-Down
    'KeyJ': 15,     // C-Left
    'KeyL': 16      // C-Right
};

window.addEventListener('keydown', (e) => {
    if (keyMap[e.code] !== undefined) {
        inputState.buttons.add(keyMap[e.code]);
        e.preventDefault();
    }
    // Arrow keys for analog stick
    if (e.code === 'ArrowUp') inputState.analogY = -32767;
    if (e.code === 'ArrowDown') inputState.analogY = 32767;
    if (e.code === 'ArrowLeft') inputState.analogX = -32767;
    if (e.code === 'ArrowRight') inputState.analogX = 32767;
});

window.addEventListener('keyup', (e) => {
    if (keyMap[e.code] !== undefined) {
        inputState.buttons.delete(keyMap[e.code]);
    }
    if (e.code === 'ArrowUp' || e.code === 'ArrowDown') inputState.analogY = 0;
    if (e.code === 'ArrowLeft' || e.code === 'ArrowRight') inputState.analogX = 0;
});
```

#### 3. Gamepad API Support

```javascript
function updateGamepad() {
    const gamepads = navigator.getGamepads();
    const gamepad = gamepads[0]; // Use first connected gamepad

    if (!gamepad) return;

    // Map gamepad buttons to N64 buttons
    inputState.buttons.clear();
    if (gamepad.buttons[0]?.pressed) inputState.buttons.add(8);  // A
    if (gamepad.buttons[1]?.pressed) inputState.buttons.add(0);  // B
    if (gamepad.buttons[9]?.pressed) inputState.buttons.add(3);  // Start
    // ... map remaining buttons

    // Analog stick (axis 0 = X, axis 1 = Y)
    inputState.analogX = Math.round(gamepad.axes[0] * 32767);
    inputState.analogY = Math.round(gamepad.axes[1] * 32767);
}
```

#### 4. Input Callbacks Implementation

```javascript
const inputPollCallback = Module.addFunction(() => {
    // Update input state from keyboard/gamepad
    updateGamepad();
}, 'v');

const inputStateCallback = Module.addFunction((port, device, index, id) => {
    // port: controller port (0-3)
    // device: RETRO_DEVICE_JOYPAD or RETRO_DEVICE_ANALOG
    // index: for analog (0=left stick, 1=right stick)
    // id: button ID or axis ID

    if (port !== 0) return 0; // Only port 0 supported

    if (device === 1) { // RETRO_DEVICE_JOYPAD
        return inputState.buttons.has(id) ? 1 : 0;
    } else if (device === 2) { // RETRO_DEVICE_ANALOG
        if (index === 0) { // Left stick
            if (id === 0) return inputState.analogX; // X axis
            if (id === 1) return inputState.analogY; // Y axis
        }
    }

    return 0;
}, 'iiiii');
```

### Part B: Audio Implementation

#### 1. Web Audio Context Setup

```javascript
const audioContext = new (window.AudioContext || window.webkitAudioContext)({
    sampleRate: 44100, // Match N64 audio output
    latencyHint: 'interactive'
});

// Create audio worklet or script processor for streaming
const audioBufferSize = 4096;
const audioQueue = [];
let audioStartTime = 0;
```

#### 2. Audio Sample Callback

The core can call audio callbacks in two ways:
- `audio_sample(left, right)` - single stereo sample
- `audio_sample_batch(data, frames)` - batch of samples

```javascript
const audioSampleCallback = Module.addFunction((left, right) => {
    // Convert int16 samples to float32 [-1.0, 1.0]
    const leftFloat = (left << 16 >> 16) / 32768.0;  // Sign extend
    const rightFloat = (right << 16 >> 16) / 32768.0;

    audioQueue.push(leftFloat, rightFloat);

    // Flush to Web Audio when buffer is full
    if (audioQueue.length >= audioBufferSize * 2) {
        flushAudioBuffer();
    }
}, 'vii');

const audioSampleBatchCallback = Module.addFunction((data, frames) => {
    // data = pointer to int16 stereo samples
    // frames = number of stereo sample pairs

    for (let i = 0; i < frames; i++) {
        const left = Module.HEAP16[data/2 + i*2];
        const right = Module.HEAP16[data/2 + i*2 + 1];

        audioQueue.push(left / 32768.0, right / 32768.0);
    }

    if (audioQueue.length >= audioBufferSize * 2) {
        flushAudioBuffer();
    }

    return frames;
}, 'iii');
```

#### 3. Audio Buffer Management

```javascript
function flushAudioBuffer() {
    if (audioQueue.length === 0) return;

    // Create audio buffer
    const frames = audioQueue.length / 2;
    const buffer = audioContext.createBuffer(2, frames, audioContext.sampleRate);

    // Fill left and right channels
    const leftChannel = buffer.getChannelData(0);
    const rightChannel = buffer.getChannelData(1);

    for (let i = 0; i < frames; i++) {
        leftChannel[i] = audioQueue[i * 2];
        rightChannel[i] = audioQueue[i * 2 + 1];
    }

    // Schedule playback
    const source = audioContext.createBufferSource();
    source.buffer = buffer;
    source.connect(audioContext.destination);

    const playTime = Math.max(audioContext.currentTime, audioStartTime);
    source.start(playTime);
    audioStartTime = playTime + buffer.duration;

    // Clear queue
    audioQueue.length = 0;
}
```

#### 4. Register Audio Callbacks

```javascript
Module._retro_set_audio_sample(audioSampleCallback);
Module._retro_set_audio_sample_batch(audioSampleBatchCallback);
```

**Note:** Need to export `retro_set_audio_sample_batch` in Makefile if not already exported.

### Part C: Continuous Emulation Loop

#### 1. RequestAnimationFrame Loop

```javascript
let isRunning = false;
let frameCount = 0;
let lastTime = performance.now();

function emulationLoop() {
    if (!isRunning) return;

    // Run one frame of emulation
    Module._retro_run();

    frameCount++;

    // FPS counter
    const now = performance.now();
    if (now - lastTime >= 1000) {
        console.log(`FPS: ${frameCount}`);
        frameCount = 0;
        lastTime = now;
    }

    // Schedule next frame (target 60 FPS)
    requestAnimationFrame(emulationLoop);
}

// Start emulation
isRunning = true;
emulationLoop();
```

#### 2. Pause/Resume Controls

```html
<button id="pauseBtn">Pause</button>
<button id="resumeBtn">Resume</button>
```

```javascript
document.getElementById('pauseBtn').addEventListener('click', () => {
    isRunning = false;
});

document.getElementById('resumeBtn').addEventListener('click', () => {
    if (!isRunning) {
        isRunning = true;
        lastTime = performance.now();
        emulationLoop();
    }
});
```

## Expected Outcomes

### Success Criteria

- ✅ Keyboard input controls N64 emulator
- ✅ Gamepad input recognized and mapped correctly
- ✅ Audio plays without crackling or stuttering
- ✅ Emulation runs at consistent ~60 FPS
- ✅ Game responds to controller input (menu navigation, gameplay)
- ✅ Audio synchronized with video
- ✅ Pause/resume functionality works

### Expected Console Output

```
Audio context initialized: 44100 Hz
Input handlers registered
Starting emulation loop...
FPS: 60
FPS: 59
FPS: 60
Audio buffer flushed: 2048 samples
Gamepad connected: Xbox Controller
FPS: 60
```

### Expected Behavior

**WaveRace.n64 should:**
1. Display Nintendo 64 boot logo with sound
2. Show game title screen with music
3. Navigate menus with D-pad/analog stick
4. Select options with A button
5. Start gameplay with interactive controls
6. Play game audio and sound effects
7. Render at smooth ~60 FPS

### Potential Issues

1. **Audio crackling:**
   - Buffer underruns (queue too small)
   - Wrong sample rate (N64 uses ~32000-48000 Hz)
   - Need larger `audioBufferSize`

2. **Input lag:**
   - `inputPollCallback` not called frequently enough
   - Gamepad API polling delay

3. **Performance drops:**
   - WASM execution too slow
   - Graphics plugin overhead
   - Disable debugging output for better performance

4. **Audio/video desync:**
   - Core expects audio pacing for timing
   - May need frame skipping logic

5. **Button mapping confusion:**
   - N64 has unique button layout (C-buttons, Z trigger)
   - Need clear on-screen guide

## File Modifications

**test_libretro.html:**
- Add keyboard event listeners
- Add gamepad polling
- Add Web Audio setup
- Add audio sample callbacks
- Add emulation loop
- Add pause/resume controls
- Add on-screen control guide

**Makefile:**
- Verify `retro_set_audio_sample_batch` is exported
- May need `-s EXPORTED_FUNCTIONS` update

## Validation Tests

1. ✅ **Keyboard Input** - Press keys, verify button state changes
2. ✅ **Gamepad Detection** - Connect gamepad, verify recognized
3. ✅ **Analog Stick** - Move stick, verify in-game response
4. ✅ **Audio Playback** - Hear boot sound and music
5. ✅ **60 FPS Sustained** - Monitor FPS counter over 30 seconds
6. ✅ **Menu Navigation** - Use D-pad to navigate game menus
7. ✅ **Gameplay** - Start race in WaveRace, control vehicle
8. ✅ **Pause/Resume** - Verify emulation pauses correctly

## Performance Expectations

- **Target FPS:** 60 (may drop to 50-55 on complex scenes)
- **Audio latency:** <100ms
- **Input latency:** <50ms (2-3 frames)
- **CPU usage:** 30-80% (single core)

## Debugging Tips

1. **Test input without emulation:**
   ```javascript
   console.log('Button state:', Array.from(inputState.buttons));
   ```

2. **Monitor audio queue:**
   ```javascript
   console.log('Audio queue size:', audioQueue.length);
   ```

3. **Measure frame time:**
   ```javascript
   const start = performance.now();
   Module._retro_run();
   console.log('Frame time:', performance.now() - start, 'ms');
   ```

4. **Check for WASM errors:**
   - Look for exceptions in console
   - Check for memory access violations

## Code Locations

- **libretro/libretro.c:**
  - `retro_set_input_poll()` (~line 950)
  - `retro_set_input_state()` (~line 955)
  - `retro_set_audio_sample()` (~line 960)
- **mupen64plus-core/src/plugin/plugin.h:**
  - Input plugin interface
- **mupen64plus-core/src/pi/pi_controller.c:**
  - N64 controller emulation

## Next Steps

After completing this milestone, the emulator is fully functional! Potential enhancements:

1. **Save States** - Implement serialization/deserialization
2. **Cheats** - Support GameShark codes
3. **Multiple controllers** - 4-player support
4. **Fullscreen mode** - Better gaming experience
5. **Mobile controls** - On-screen touch controls
6. **ROM selection UI** - Load different games
7. **Performance profiling** - Optimize slow paths
8. **Compatibility testing** - Test multiple ROMs

## Notes

- Web Audio requires user gesture to start (click "Resume" button)
- Gamepad API requires page focus to receive events
- Safari has different Web Audio behavior (may need polyfills)
- Frame pacing is critical - audio callbacks help synchronize timing
- N64 emulation is CPU-intensive; modern devices recommended
