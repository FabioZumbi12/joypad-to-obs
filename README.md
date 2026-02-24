![Joypad to OBS](img/banner.svg)

# Joypad to OBS

[![GitHub Release](https://img.shields.io/github/v/release/FabioZumbi12/joypad-to-obs)](https://github.com/FabioZumbi12/joypad-to-obs/releases/latest) [![Build Status](https://github.com/FabioZumbi12/joypad-to-obs/actions/workflows/push.yaml/badge.svg)](https://github.com/FabioZumbi12/joypad-to-obs/actions)

## Overview

**Joypad to OBS** is a powerful plugin that turns any connected gamepad, joystick, or even custom Arduino devices into a fully functional macro controller for OBS Studio.
 
This plugin **does not** provide a visual overlay for your controller. Its purpose is to use your controller as a remote control for OBS, allowing you to bind buttons and axes to actions like switching scenes, muting audio, and controlling media. It provides a physical, tactile way to manage your stream without needing a dedicated macro pad.

## Features

*   **Control OBS with Your Gamepad:** Map buttons and analog sticks to a wide range of OBS actions.
*   **Direct Hardware Access:** No intermediate software needed. The plugin communicates directly with any controller recognized by the operating system (e.g., in Windows' "joy.cpl" Game Controllers panel).
*   **Platform Support:** Primarily developed and tested for **Windows**. macOS and Linux input paths exist, but are currently experimental.
*   **Broad Device Compatibility:**
    *   **Windows Input Backends:**
        *   **XInput:** Used for Xbox controllers and devices that expose an XInput interface.
        *   **DirectInput:** Used for HID/generic controllers and other non-XInput game controllers.
        *   **Auto de-duplication:** When both APIs expose the same physical controller, the plugin avoids duplicate entries/actions.
    *   **Custom Hardware:** Compatible with **Arduino-based devices** that appear as a standard joystick, allowing you to use potentiometers, encoders, and sliders to control OBS.
*   **Intuitive UI:** A dedicated "Tools" menu dialog to create, edit, and manage all your bindings.
*   **"Learn" Mode:** Simply press a button or move an axis on your controller to assign it to an action.
*   **Advanced Axis Configuration:** Calibrate axis range (Min/Max), set deadzones (Threshold), and invert axis direction for precise control.
*   **OBS Hotkey to Pause/Resume Controller Listening:** Toggle all gamepad input processing on/off from OBS Hotkeys.
*   **OSD Feedback for Listening State:** When the listening toggle hotkey is used, an OSD message shows whether listening is On or Off.

## Supported Actions

Joypad to OBS supports a comprehensive set of commands to manage your stream:

### 🎬 Scenes & Transitions
*   **Switch Scene:** Instantly cut to a specific scene.
*   **Next Scene** and **Previous Scene:** Cycle through the scene list.
*   **Toggle Studio Mode:** Turn Studio Mode on/off.
*   **Transition Preview to Program:** Trigger transition from Preview to Program.

### 👁️ Sources & Filters
*   **Toggle Source Visibility:** Toggle visibility (show/hide) for a source.
*   **Set Source Visibility:** Force source visibility to show or hide.
*   **Toggle Filter:** Toggle enabled state of a source filter.
*   **Set Filter:** Force a source filter enabled/disabled state.

### 🔊 Audio Mixing
*   **Toggle Source Mute:** Toggle mute for an audio source.
*   **Set Source Mute:** Force mute state on/off.
*   **Set Source Volume:** Set a fixed dB value.
*   **Adjust Source Volume:** Apply relative volume step up/down.
*   **Set Source Volume (Slider):** Map axis input to volume percent with configurable gamma curve.

### ⏯️ Media Playback
*   **Media Play/Pause**
*   **Media Restart**
*   **Media Stop**

### 📡 Output & Tools
*   **Toggle Streaming:** Start/stop streaming.
*   **Toggle Recording:** Start/stop recording.
*   **Toggle Virtual Camera:** Start/stop virtual camera.

## Profile Management

Joypad to OBS allows you to create and manage multiple profiles, each with its own set of bindings. This is useful for different games, scenes, or streaming setups.

*   **Create:** Click the **+** button to create a new blank profile.
*   **Rename:** Click the **✎** button to rename the current profile.
*   **Duplicate:** Click the **❐** button to create a copy of the current profile.
*   **Remove:** Click the **-** button to delete the current profile.
*   **Import/Export:** Use the **Import** and **Export** buttons to share profiles or back them up as JSON files.

## Hotkeys

When you create a profile, the plugin automatically registers a corresponding **OBS Hotkey**. This allows you to switch between profiles using your keyboard or another device (like a Stream Deck).

1.  Go to **File** -> **Settings** -> **Hotkeys** in OBS.
2.  Search for "Joypad to OBS".
3.  You will see entries like `Joypad to OBS: Switch to profile 'MyProfile'`.
4.  Assign any key combination you like.

When triggered, the plugin will instantly load the bindings for that profile.

The plugin also registers a global hotkey:

*   `Joypad to OBS: Toggle gamepad listening`

Use it to quickly disable/enable all controller-triggered actions without removing bindings.  
If OSD notifications are enabled, this hotkey also shows the current listening state (`On`/`Off`) on screen.

## Requirements

*   **OBS Studio:** Version 28 or newer.
*   **Operating System:** Windows 10/11 (recommended). macOS and modern Linux distributions are available in experimental mode.

## How to Use

1.  **Install the plugin:** Download the latest release and copy the files to your OBS Studio plugins directory.
    *   **Windows:** `C:\Program Files\obs-studio\obs-plugins\64bit\`
    *   **macOS:** `~/Library/Application Support/obs-studio/plugins/`
    *   **Linux:** `~/.config/obs-studio/plugins/`
2.  **Open the configuration window:** In OBS, go to the **Tools** menu and select **Settings for Joypad to OBS**.
3.  **Select a Profile:** Choose the profile you want to edit from the dropdown menu, or create a new one.
4.  **Add a new binding:**
    *   Click the **"Add Command"** button.
    *   Click **"Listen"** and press the button or move the axis on your controller that you want to use.
    *   Select the **Action** you want to perform (e.g., "Switch Scene", "Set Source Mute").
    *   Configure the target (e.g., choose the specific scene or source).
    *   Click **OK** to save the binding.
5.  Your controller is now ready to control OBS!
6.  (Optional) In **OBS Settings -> Hotkeys**, assign `Joypad to OBS: Toggle gamepad listening` to quickly pause/resume controller input while streaming.

## Building from Source

If you want to build the plugin yourself, clone the repository recursively and use CMake.

```bash
git clone --recursive https://github.com/FabioZumbi12/joypad-to-obs.git
cd joypad-to-obs
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Simulate GitHub Actions Build (Windows)

To run a local build flow close to the `windows-2022` GitHub Actions job, use:

```powershell
pwsh -File .\scripts\ci-local-windows.ps1
```

Useful options:

```powershell
# Simulate fresh runner state (slowest, most faithful)
pwsh -File .\scripts\ci-local-windows.ps1 -CleanBuild -CleanDeps -CleanRelease

# Build only (skip packaging)
pwsh -File .\scripts\ci-local-windows.ps1 -Package:$false

# Enable verbose CI-like debug logs
pwsh -File .\scripts\ci-local-windows.ps1 -DebugLogs
```

## License
This plugin is released under the GPLv2 license.

## Support the Project

If Joypad to OBS helps your workflow, consider supporting the project or following my work here:  
https://linktr.ee/FabioZumbi12
