# AGENTS.md - Animalese Typing Desktop

> Guidelines for AI coding agents working in this Electron desktop application codebase.

## Project Overview

Animalese Typing is a cross-platform Electron desktop app that plays Animal Crossing-style
"Animalese" sound effects as users type. It uses native key listeners (C++/Swift) to capture
global keyboard input and Howler.js for audio playback.

**Tech Stack:** Electron, Node.js, Howler.js, jQuery, Native C++/Swift key listeners

## Build & Run Commands

### Development Setup
```bash
npm install                    # Install dependencies
npm start                      # Run app in development mode
```

### Building Native Key Listeners (required before first run)
```bash
# Linux (requires libx11-dev, libxtst-dev)
npm run build:linux-listener

# macOS
npm run build:mac-listener

# Windows (requires g++)
npm run build:win-listener
```

### Full Platform Builds
```bash
npm run build:linux    # Builds listener + AppImage/deb
npm run build:mac      # Builds listener + dmg
npm run build:win      # Builds listener + exe installer
```

Output artifacts are placed in `/exports/` directory.

### Testing
**No test framework is currently configured.** Manual testing is required.
When adding tests in the future, consider using Electron's testing utilities or Playwright.

### Linting
**No linter configured.** Only Prettier is used for formatting.

## Architecture

### Process Model
- **Main Process** (`main.js`): ESM module, handles app lifecycle, tray, IPC, spawns native listener
- **Preload** (`preload.cjs`): CommonJS, bridges main/renderer via `contextBridge`
- **Renderer** (`renderer/*.cjs`): CommonJS, UI logic loaded via `<script>` tags in HTML

### Module System
| Location | Format | Extension |
|----------|--------|-----------|
| Main process | ESM | `.js` |
| Preload/Renderer/Utils | CommonJS | `.cjs` |

### Key Components
- `utils/audio-manager.cjs` - Sound playback via Howler.js
- `utils/keycode-to-sound.cjs` - Platform-specific keycode mappings
- `utils/translator.cjs` - i18n translation system
- `libs/key-listeners/` - Native C++/Swift global key capture

### IPC Communication Pattern
```javascript
// Main process (main.js)
ipcMain.handle('store-set', async (e, key, value) => { ... });
ipcMain.on('close-window', (e) => { ... });

// Preload (preload.cjs) - expose to renderer
contextBridge.exposeInMainWorld('api', {
  closeWindow: () => ipcRenderer.send('close-window'),
});

// Renderer - use exposed API
window.api.closeWindow();
```

## Code Style Guidelines

### Formatting (Prettier)
- **Semicolons:** Always use semicolons
- **Quotes:** Single quotes for strings
- **No max line length enforced**

### Naming Conventions
| Type | Convention | Example |
|------|------------|---------|
| Variables/Functions | camelCase | `voiceProfile`, `updateWindow()` |
| Settings keys | snake_case | `voice_profile`, `always_active` |
| CSS classes/IDs | snake_case | `#voice_type`, `.audio_editor` |
| Constants | UPPER_SNAKE_CASE | `SYSTRAY_ICON`, `CUTOFF_DURATION` |

### File Organization
```
main.js              # Main process entry (ESM)
preload.cjs          # Preload script (CJS)
editor.html          # Main window HTML
remap.html           # Key remap window HTML
renderer/            # Renderer process scripts
utils/               # Shared utility modules
assets/
  ├── audio/         # Sound files (.ogg)
  ├── lang/          # Translation JSON files
  ├── layouts/       # Sound board layout definitions
  ├── styles/        # CSS files
  └── svg/           # Icon SVGs
libs/key-listeners/  # Native key listener source
```

### Code Section Comments
Use `#region` style comments to organize code sections:
```javascript
//#region Key Remapper
// ... code ...
//#endregion
```

### Module Exports (CommonJS)
```javascript
// Single export
module.exports = { createAudioManager };

// Multiple exports
module.exports = {
  loadLanguage,
  updateHtmlDocumentTranslations
};
```

### Error Handling
- Use `console.error()` for errors, `console.warn()` for warnings
- Wrap async operations in try/catch
- Check for null/undefined before accessing properties
```javascript
if (!focusedWindow?.owner?.name) return;
```

### Author Comments
File headers may include author attribution:
```javascript
/**
 * author: joshxviii 
 */
```

## Important Patterns

### Audio Path Format
Sounds are referenced using a path format:
- `&.a` - Voice sound (letter 'a')
- `%.60` - Instrument note (MIDI note 60)
- `sfx.enter` - Sound effect
- `#no_sound` - Special command (no audio)

### Settings/Preferences
Settings are managed via `electron-store`:
```javascript
// In preload - exposed as window.settings
window.settings.get('volume');
window.settings.set('volume', 0.5);
window.settings.reset('volume');
```

### Custom HTML Elements
Custom elements are defined for UI components:
```javascript
customElements.define('svg-button', class extends HTMLElement {
  connectedCallback() { ... }
});
```

### Translation System
Translations use a `translation` attribute in HTML:
```html
<h2 translation="settings.general"></h2>
```
Translation files are in `assets/lang/*.json`.

## Common Development Tasks

### Adding a New Sound Effect
1. Add `.ogg` file to `assets/audio/`
2. Update sprite map in `utils/audio-manager.cjs` if using sprites
3. Reference in layouts (`assets/layouts/*.json`) or keycode mappings

### Adding a New Translation
1. Add key-value pair to all files in `assets/lang/`
2. Use `translation="key.name"` attribute in HTML

### Adding a New Setting
1. Add default value in `main.js` `defaults` object
2. Add IPC handler if needed
3. Access via `window.settings.get/set` in renderer

### Platform-Specific Code
Check platform with:
```javascript
process.platform  // 'win32', 'darwin', 'linux'
```
