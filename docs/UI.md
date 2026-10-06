# Shared UI theme

`src/ui/Theme.hpp` is the base design for developer panels: opaque charcoal
surfaces, off-white text, a muted olive accent, thin borders, and compact spacing.
Status colors are available for success, warnings, and errors; also use a text
label or icon so color is never the only way to communicate status.

## Application setup

After creating the ImGui context, before the first frame:

```cpp
#include "ui/Theme.hpp"

ImGui::CreateContext();
voxel::ui::applyTheme();
```

All windows and widgets in that context inherit the style. Apply it once at
startup, or again when changing the theme or display scale, not in each panel.
`ui::Overlay` now owns the context and GLFW/Vulkan backends and applies the theme
at startup. The renderer draws UI after the world and rebuilds the UI Vulkan
resources when the swapchain changes. Do not create a second context for panels.

## Console shell

Press the backtick/tilde key (`~` on a US keyboard, with or without Shift) to
toggle the console. Drag the title bar to move it, use the bottom-right grip to
resize it, or click the top-right X to close it. Layout is retained while the
game is running; it is not saved to disk yet.

The console is a floating window with modal game-input behavior: camera look,
movement, and block actions are disabled while it is visible, and the cursor is
released. Terrain workers and rendering continue.

The bottom row contains a single-line textbox and Submit button. Enter and the
button both store the text and clear the draft; empty or whitespace-only lines
are ignored. The textbox receives focus when opening and after submission.

`ConsolePanel::submissions()` provides read-only access to pending text, and
`takeSubmission()` consumes the oldest line. `Renderer::takeConsoleSubmission()`
exposes the same queue to the application. Text is stored exactly as entered
and remains available after closing the panel. Nothing executes it yet.
Input is limited to 1023 UTF-8 bytes, with at most 128 pending lines; when full,
the draft is kept and Submit is disabled until a consumer drains the queue.
The area above the input row displays messages in a scrollable child panel.
Submitting echoes the text with a `> ` prefix, without executing it.

## Message output

Drag across output text to select characters or multiple lines. Shift-click
extends the selection. With the output focused, Ctrl+A selects retained output
and Ctrl+C copies it, including labels and source names. Right-click provides
Copy, Select all, and Paste into input. Ctrl+V while output is focused also
places clipboard text into the input draft; it never edits log history.
Multiline output pasted this way becomes a single line with spaces, and nothing
is submitted until Enter or Submit. The input textbox also supports its normal
selection, Ctrl+C, Ctrl+X, and Ctrl+V editing shortcuts.

Logging is independent of ImGui and safe to call before UI startup or from
worker threads. Existing `console::info()` and `console::error()` calls now
write to both the terminal and retained in-game history. Additional helpers:

```cpp
console::warning("Chunk queue is full");
console::error("Could not load chunk; continuing with generated terrain");
console::response("Render distance is 4");
console::success("World saved");
console::debug("Mesh job completed");
console::write("network", "Connected to server", "Networking");
```

Info, commands, and responses use off-white; warnings use yellow, errors use
red, success uses green, and debug uses muted text. Warnings and errors include
labels, so severity is not communicated through color alone. Unknown message
types fall back to normal text. Logging an error does not terminate the game.

Register or override a style on the main thread with:

```cpp
panel.setMessageStyle("network", {{0.4F, 0.7F, 0.9F, 1.0F}, "Network", ""});
```

The fields are color, label, and prefix. A style change affects retained messages
of that type too. The optional channel appears as a source label. Display text
is literal; percent signs and markup are not interpreted as formatting.

History retains the newest 2048 lines. Long messages are split into lines of at
most 4096 bytes, preserving UTF-8 character boundaries. Only new messages are
copied from the shared log, and only visible rows are rendered. Lines do not
wrap; horizontal scrolling keeps long entries accessible. New output follows
the bottom when already there; scrolling up stops following. Submitting your
own line returns to the latest output. History is session-only.

## Customization

Change the defaults in `Theme.hpp` to update the shared palette, and `Theme.cpp`
to update shared spacing or widget appearance. A custom palette can also be
passed to `applyTheme(theme, scale)`.

For a local override, push a style and pop it within the same drawing scope:

```cpp
const voxel::ui::Theme theme;
ImGui::PushStyleColor(ImGuiCol_Text, theme.warning);
ImGui::TextUnformatted("Streaming queue is full");
ImGui::PopStyleColor();
```

Keep pushes and pops balanced so overrides cannot leak into other panels.
Reuse the palette for custom drawings instead of inventing per-panel colors.

`makeStyle(theme, scale)` creates a fresh style without requiring a context.
Scaling starts from base measurements every time rather than compounding the
previous scale. Font selection is left to UI initialization so the theme does
not depend on machine-specific font paths or runtime asset loading.
