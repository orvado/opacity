# Opacity – Windows File Manager Replacement

## 1. Project Overview

- **Name:** Opacity
- **Platform:** Windows 10+ (x64, optional ARM64 later)
- **Language:** C++
- **GUI Framework:** Dear ImGui (with appropriate backend: Win32 + DirectX 11/12)
- **Purpose:** Full-featured, high-performance replacement for Windows File Explorer, focused on power-user workflows, advanced search/filtering, rich previews, and robust diff/comparison tools.

## 2. High-Level Goals

- Provide a **fast, responsive, single-EXE** (plus plugins) file manager.
- Support **multi-pane** and **tabbed** browsing for efficient workflows.
- Offer **advanced search, filtering, and sorting** (including content search and saved searches).
- Integrate **rich previews** (images, video, audio, text, archives, documents via handlers).
- Implement **file and folder comparison (diff)** capabilities.
- Respect Windows conventions (shell integration, keyboard shortcuts) while improving ergonomics.
- Be **extensible/modular** to allow future plugins or scriptable extensions.
- Provide **modern UX** with smooth animations, transitions, and visual feedback.
- Support **dark mode** natively with proper theming throughout.
- Enable **keyboard-first navigation** for power users while maintaining mouse-friendly interface.

## 3. Core Architecture

### 3.1 Application Structure

- **Entry point:** Win32 API main window with Dear ImGui context initialization.
- **Rendering backend:** DirectX 11 or 12 (TBD; DX11 for simplicity initially).
- **App loop:** Custom main loop integrating message pump and ImGui frame rendering.
- **Subsystems:**
  - `Core`: configuration, logging, settings, hotkeys, command dispatcher.
  - `Filesystem`: path, directory listing, file operations, metadata, monitoring, drive enumeration.
  - `UI`: layouts, panels, dialogs, theming, popups, notifications, animations.
  - `Search`: indexing (optional), search query engine, filters, regex support.
  - `Preview`: preview handlers for common file types, thumbnail cache.
  - `Diff`: file and folder comparison engine and viewers.
  - `Integration`: shell integration, context menu hooks, default app registration.
  - `Clipboard`: advanced clipboard manager with history and formats.
  - `Bookmarks`: favorites, recent locations, tagged folders.

### 3.2 Data Model

- **Path abstraction:** Unified `Path` type wrapping Windows `std::filesystem::path` and Win32 APIs.
- **Item model:** `FsItem` with properties:
  - Name, full path, type (file/folder/symlink/junction/device), size, timestamps.
  - Attributes (read-only, hidden, system, archive, compressed, encrypted).
  - Content type/MIME (best-effort) and associated icon.
  - Color tags/labels (user-assigned).
  - Custom metadata (user notes, ratings).
- **View model:** `PaneState`, `TabState`, `LayoutState` for current directory, selection, and UI state.
- **Search model:** `SearchQuery`, `SearchResultItem`, `FilterDescriptor`.
- **Operation model:** `FileOperation` (copy/move/delete), `OperationQueue`, progress tracking.
- **History model:** `NavigationHistory`, `ClipboardHistory`, `SearchHistory`.

### 3.3 Multi-Threading & Performance

- Background worker threads for:
  - Directory enumeration and metadata fetching.
  - Thumbnail generation and file previews.
  - Search (name/content/metadata).
  - Diff operations.
- Use thread-safe queues and async task dispatchers.
- Minimize UI thread blocking; show progress indicators for long operations.

## 4. User Interface & UX

### 4.1 Main Window Layout

- **Components:**
  - Menu bar (or command palette style) for global actions.
  - Toolbar with common operations and path navigation (customizable).
  - Optional drive bar and favorites/quick access sidebar.
  - Central area with **one or more panes**, each with **tabs**.
  - Bottom status bar (item count, size, selection, current operation status, disk space).
  - Optional breadcrumb navigation bar.
  - Floating/dockable preview panel.
  - Minimap for large directories (optional phase 2).

### 4.2 Pane & Tab Management

- **Tabs:**
  - Open multiple tabs per pane.
  - Support duplicate tab, pin tab, close all/others.
  - Remember history for back/forward.
- **Panes:**
  - Single-pane and dual-pane layouts (future: tri-pane).
  - Vertical and horizontal split support.
  - Hotkeys for switching panes and moving/copying between panes.

### 4.3 Navigation & Address Bar

- **Path bar:**
  - Breadcrumb-style clickable segments (e.g., `C: > Users > Ken > Documents`).
  - Direct path input with completion and history.
- **Navigation history:**
  - Per-tab back/forward and up (parent) navigation.
- **Drives and favorites:**
  - List of drives (letters, network drives, UNC paths).
  - User-configurable favorites/quick access locations with drag-and-drop.

### 4.4 Item View Modes

- **View options:**
  - Details view (columns: name, size, type, modified, attributes, etc.).
    - Resizable, reorderable columns.
    - Column presets (save/load column configurations).
  - List view (compact, no columns).
  - Tiles/Icons view (small/medium/large/extra-large icons with previews when available).
  - Gallery mode for image/video folders (filmstrip/grid with adjustable size).
  - Tree view (hierarchical folder structure in single pane).
- **Sorting:**
  - Sort by name (natural/alphabetical), size, type, date created/modified/accessed, extension, color tag, etc.
  - Ascending/descending, stable sort, multi-column sorting (secondary sort key optional).
  - Custom sort orders (user-defined rules).
- **Grouping (optional phase 2):** group items by type, date, size ranges, first letter, color tag, etc.

### 4.5 Selection & Interaction

- **Selection:**
  - Single-click, Ctrl-click, Shift-click, keyboard-based selection.
  - Select all/invert selection/select none.
  - Select by pattern (e.g., `*.png`, regex in advanced mode).
  - Select by filter (files matching current search/filter criteria).
  - Persistent selection across directory changes (optional).
  - Visual indication of cut/copied items.
- **Drag-and-drop:**
  - Internal drag-and-drop between panes/tabs.
  - Visual feedback during drag (ghost image, drop target highlight).
  - Modifier keys for copy vs move (Ctrl to copy, Shift to move, Alt for link).
  - Drag files out to Explorer/other apps (if feasible with ImGui + Win32 interop).
  - Accept drops from external apps/Explorer.
  - Drop zone indicators.
- **Context menus:**
  - Opacity-specific context menu (open, copy path, open with, diff, etc.).
  - Copy as path, copy as name, copy as UNC path.
  - Open terminal here (PowerShell/CMD/custom).
  - Optional integration of Windows shell context menu (phase 2).
  - Custom actions (user-defined commands).

### 4.6 Theming & Customization

- **Themes:**
  - Light/dark themes, custom accent colors.
  - Font size and family (per user setting).
- **Layout presets:**
  - Save/restore UI layouts (panes, tabs, columns).
- **Hotkeys:**
  - Rebindable keyboard shortcuts with export/import.

## 5. File System Capabilities

### 5.1 Basic Operations

- Open, copy, move, rename, delete (to recycle bin and permanent), new (folder/file).
- Cut/copy/paste with clipboard integration.
- Multi-file operations with progress dialog and ability to cancel/skip/replace/retry.
  - Pause/resume for long operations.
  - Queue multiple operations.
  - Speed throttling (limit I/O for background operations).
  - Conflict resolution strategies (skip all, overwrite all, rename, etc.).
- Display effective permissions and basic security info (phase 2).
- Calculate folder sizes (on-demand with caching).
- Verify file integrity (checksum comparison after copy).

### 5.2 Advanced Operations

- **Batch rename:**
  - Rename multiple files with patterns, sequences, search/replace, case change.
  - Preview changes before applying.
  - Regex support for advanced patterns.
  - Numbering with padding (001, 002, etc.).
  - Date/time insertion.
  - EXIF data insertion for images.
- **Symbolic links and junctions:**
  - Create symlink/junction/hard link (with admin rights when required).
  - Show link targets and distinguish them visually.
  - Navigate to target location.
- **Archive handling (phase 2):**
  - Basic integration with `.zip` using Windows APIs.
  - Browse archives as virtual folders.
  - Extract with path preservation.
  - Create archives from selection.
  - Optional plugin architecture for 7z/rar/tar.gz/etc.
- **Split/merge files:**
  - Split large files into chunks.
  - Merge split files back together.
- **Duplicate finder:**
  - Find duplicate files by hash (MD5/SHA-256).
  - By name, size, or content similarity.

### 5.3 Metadata & Properties

- Property panel for selected file(s)/folder:
  - Basic details: size, path, type, timestamps, attributes.
  - Hashes (MD5/SHA-1/SHA-256/CRC32) on demand with progress.
  - EXIF/IPTC/XMP metadata for images (phase 2).
  - Media info (duration, resolution, codec, bitrate, framerate) for video/audio (phase 2).
  - Version info for executables and DLLs.
  - Digital signatures and certificate info.
  - Alternative data streams (ADS) viewer/editor.
  - Extended attributes and custom properties.
  - Tag editor (assign color tags and text labels).

### 5.4 File System Monitoring

- Use `ReadDirectoryChangesW` or equivalent to track changes.
- Auto-refresh views on external changes (new/deleted/renamed/modified items).
- Show transient notifications for significant operations.

## 6. Search & Filtering

### 6.1 Basic Search

- Search input for current folder (with optional recursion).
- Search by:
  - Name (contains/starts with/ends with, case-insensitive/sensitive).
  - Extension.
  - Wildcards (e.g., `*.cpp`, `report_??.pdf`).

### 6.2 Advanced Search

- **Search scope:**
  - Current directory.
  - Current directory + subfolders.
  - Multiple selected folders.
  - Entire drive or set of drives.
- **Criteria:**
  - Name patterns (wildcards, regex optional in phase 2).
  - Size (>, <, between, equal).
  - Date created/modified/accessed (before/after/between).
  - Attributes (hidden, system, read-only, etc.).
  - File type/extension groups (e.g., images, videos, documents).
- **Content search:**
  - Text content search in files using encodings (UTF-8/UTF-16/ANSI) with case/whole word options.
  - Optional binary/content search for patterns (phase 2).
- **Search results view:**
  - Display as virtual folder with full paths.
  - Columns for path, matched field, snippet (for text content) if feasible.
  - Actions: open location, open file, add as favorite, diff against other file.
- **Saved searches:**
  - Save search queries as reusable items.
  - Quick select from sidebar or menu.

### 6.3 Filtering

- **Inline filters:**
  - Quick filter box for current view (filter by name/extension).
  - Column-based filters (e.g., size > X, date range).
  - Real-time filtering as you type.
  - Filter history and suggestions.
- **Preset filters:**
  - Images, videos, audio, documents, archives, executables, etc.
  - User-defined custom filter presets.
  - Filter by color tag.
- **Chained filters:**
  - Combine multiple filter conditions (AND/OR in advanced UI).
  - Visual filter builder with drag-and-drop.
- **Negative filters:**
  - Exclude patterns (NOT conditions).

## 7. Preview System

### 7.1 Preview Panel

- Optional side/bottom panel that shows preview of selected item.
- Toggleable via toolbar or hotkey.
- Responsive sizing, zoom controls (where applicable).

### 7.2 Supported Preview Types

- **Images:**
  - Common formats: PNG, JPEG, GIF (including basic animated GIF playback), BMP, WebP, TIFF, SVG (phase 2), ICO, HEIF/HEIC (via codec).
  - Features: zoom, pan, rotate (display only), basic EXIF info display.
  - Slideshow mode with timer.
  - Thumbnail strip for multi-image selection.
- **Videos:**
  - Common formats via Windows Media Foundation or FFmpeg (MP4, MKV, AVI, MOV, WMV, WebM depending on codecs installed).
  - Controls: play/pause, seek, volume, playback speed, frame-by-frame stepping.
  - Thumbnail timeline preview on hover.
- **Audio:**
  - Waveform or simple playback controls for MP3, WAV, FLAC, OGG, M4A (via suitable library in phase 2).
  - Spectrum analyzer visualization (optional).
  - Metadata display (artist, album, etc.).
- **Text:**
  - Plain text, source code, JSON, XML, logs, Markdown.
  - Syntax highlighting for 50+ common languages (C++, Python, JS, etc.) using library like TextMate grammars.
  - Line numbers, word wrap toggle.
  - Hex viewer for binary files.
- **PDF / Documents (phase 2):**
  - Integrate with existing Windows preview handlers or PDF library (e.g., MuPDF, PDFium).
  - Office documents via preview handlers.
- **Archives:**
  - Show archive contents in preview panel (names, sizes, compression ratio) when possible.
  - Tree view of archive structure.
- **Fonts:**
  - Font preview with sample text (phase 2).

### 7.3 Implementation Notes

- Abstract `PreviewHandler` interface with concrete implementations per file type category.
- Throttle/limit preview generation for large files (ask user before reading huge files).
- Manage lifetime of media players and decoders carefully to avoid leaks.
- Thumbnail cache with LRU eviction policy.
- Background thumbnail generation with priority queue.
- Preview loading indicators and placeholder images.
- Fallback to icon if preview generation fails.

## 8. Diff & Comparison Features

### 8.1 File Comparison (Diff)

- Compare two files selected in:
  - Same folder.
  - Different tabs/panes.
  - Search results.
- **Detection of type:**
  - Text vs binary detection.
- **Text diff viewer:**
  - Side-by-side view with syntax highlighting (optional phase 2).
  - Line-based diff with change/added/deleted markers.
  - Scrolling sync between panes.
  - Navigation between hunks (next/previous difference).
  - Optional ignore whitespace/case options.
- **Binary diff (phase 2):**
  - Hex view with differences highlighted.

### 8.2 Folder Comparison

- Select two folders and launch comparison.
- **Comparison options:**
  - Compare by name only.
  - Compare by name + size.
  - Compare by name + size + timestamps.
  - Optional hash-based comparison for accuracy (slower).
- **Result categories:**
  - Only in left (added/deleted depending on perspective).
  - Only in right.
  - In both but different (changed).
  - In both and identical.
- **Result view:**
  - Multi-column view with indicators for status, size, timestamps, path.
  - Filters to show only differences or only specific categories.
  - Icons indicating file vs folder.
- **Operations from comparison view:**
  - Copy from left to right / right to left.
  - Delete from one side.
  - Open file diff for differing pairs.
  - Open containing folder.

### 8.3 Integration into UI

- **Context menu actions:**
  - `Mark for comparison` and `Compare with marked`.
  - `Compare with... ` to choose second file/folder via dialog.
- **Toolbar/commands:**
  - Global shortcuts for last comparison, re-run comparison, and toggling diff options.

## 9. Keyboard Shortcuts & Power-User Features

- **Navigation:**
  - Arrow keys, PageUp/PageDown, Home/End.
  - Backspace/Alt+Up: go to parent.
  - Alt+Left/Right: back/forward history.
  - Ctrl+L: focus address bar.
  - Typing starts quick search/filter.
- **Panes & Tabs:**
  - Ctrl+T: new tab, Ctrl+W: close tab.
  - Ctrl+Shift+T: reopen closed tab.
  - Ctrl+Tab / Ctrl+Shift+Tab: cycle tabs.
  - Ctrl+1-9: jump to tab by number.
  - F6 or Tab (in pane mode): switch panes.
  - Ctrl+\: split pane.
- **File operations:**
  - F2: rename, Delete: delete, Shift+Delete: permanent delete.
  - Ctrl+C/X/V: copy/cut/paste.
  - Ctrl+Shift+C: copy path.
  - Ctrl+Shift+N: new folder, Ctrl+Shift+T: new file.
  - Ctrl+D: duplicate file/folder.
  - Ctrl+A: select all, Ctrl+I: invert selection.
  - Space: quick preview toggle.
- **Search & filter:**
  - Ctrl+F: search in current folder.
  - Ctrl+Shift+F: advanced search dialog.
  - Ctrl+E or /: quick filter.
  - Esc: clear filter.
- **View:**
  - Ctrl++ / Ctrl+-: increase/decrease icon size.
  - Ctrl+1/2/3/4/5: switch view modes.
  - F5: refresh view.
  - Ctrl+H: toggle hidden files.
- **Diff:**
  - Ctrl+K: mark for comparison.
  - Ctrl+Shift+K: compare with marked.
- **Bookmarks:**
  - Ctrl+B: toggle bookmark sidebar.
  - Ctrl+D: add current location to bookmarks.
- **Command palette (phase 2):**
  - F1 or Ctrl+Shift+P for fuzzy command execution.
  - Ctrl+P: quick navigation to file/folder.

## 10. Settings, Profiles, and Persistence

- **Settings storage:**
  - JSON config in user profile directory (`%APPDATA%\Opacity`).
  - Backup and restore settings.
  - Export/import settings for sharing.
  - Cloud sync option (phase 2).
- **Profiles:**
  - Separate profiles for UI layout and preferences (optional phase 2).
  - Quick profile switching.
  - Per-profile hotkeys and themes.
- **Session restore:**
  - Option to restore previous session (tabs, panes, last paths) on startup.
  - Crash recovery with auto-saved state.
  - Manual session save/load.
- **History:**
  - Navigation history (per-tab).
  - Search history.
  - Clipboard history.
  - Recent files and folders.
  - Configurable retention period.

## 11. Integration with Windows

- **Shell integration (phase 2+):**
  - Register as default file manager for folders (where possible).
  - Add `Open with Opacity` to folder and drive context menus.
  - Send To menu integration.
  - Desktop shortcut with icon.
- **File associations:**
  - Open certain file types within Opacity (e.g., text, images) if configured.
  - Custom handlers for specific extensions.
- **Environment & startup:**
  - Command line arguments for opening specific paths or performing operations.
  - Optional single-instance behavior with IPC to send new requests to existing instance.
  - Windows Terminal integration (open terminal at current path).
  - Admin elevation when needed (UAC prompts).
- **System tray:**
  - Minimize to tray option.
  - Quick actions from tray menu.
- **Windows Search integration (phase 2):**
  - Query Windows Search index for faster searches.
- **OneDrive/cloud storage indicators:**
  - Show sync status icons for cloud-synced files.

## 12. Logging, Diagnostics, and Error Handling

- **Logging:**
  - File-based logging with adjustable verbosity.
  - Per-subsystem log categories (filesystem, UI, search, preview, diff).
- **Error handling:**
  - Non-blocking error dialogs and notifications.
  - Detailed error info (Win32 error codes, suggested actions).
- **Diagnostics tools:**
  - View recent log entries from within UI (phase 2).

## 13. Extensibility & Plugin Model (Phase 2+)

- **Plugin architecture:**
  - Load external modules (DLLs) for extra previews, search providers, or diff algorithms.
- **Scripting integration (optional):**
  - Integrate with scripting language (e.g., Lua or Python) for automation.

## 14. Non-Functional Requirements

- **Performance:**
  - Must list 10,000+ files per directory without noticeable lag.
  - Asynchronous operations with progress indicators for tasks > 300 ms.
  - Virtual scrolling for large directories (only render visible items).
  - Target 60 FPS UI rendering.
  - Startup time < 1 second on SSD.
- **Memory usage:**
  - Keep baseline memory footprint reasonable (< 150 MB target, excluding large caches).
  - Efficient caching with configurable limits.
  - Memory-mapped file reading for large files.
- **Stability:**
  - Graceful failure of individual subsystems (e.g., a preview handler crash should not terminate the whole app where possible).
  - No data loss on crash (operation recovery).
  - Extensive error handling and logging.
- **Security:**
  - Avoid executing arbitrary code from untrusted sources.
  - Sanitize paths and operations to prevent accidental destructive actions.
  - Confirmation dialogs for destructive operations.
  - Secure deletion option (overwrite data).
- **Accessibility:**
  - Screen reader support where feasible with ImGui.
  - High contrast mode support.
  - Configurable font sizes.
  - Keyboard navigation for all features.
- **Localization (phase 3):**
  - Support for multiple languages.
  - RTL language support.
  - Date/time format localization.

## 15. Phased Implementation Plan (High-Level)

### Phase 1 – Core Explorer (MVP)

- App skeleton with ImGui + DX11/Win32 backend.
- Single-pane, tabbed file browser with details and icon views.
- Basic file operations (open/copy/move/delete/rename/new).
- Copy/paste with progress dialog.
- Basic search (name-based) and simple filters.
- Text and image previews.
- Navigation (back/forward/up, address bar).
- Favorites/bookmarks sidebar.
- Drive enumeration and selection.
- Settings system (JSON config).
- Keyboard shortcuts (core set).

### Phase 2 – Power Features

- Dual-pane layout and full tab management.
- Advanced search with filters and saved searches.
- Content search in files.
- Expanded previews (video/audio, archives, richer metadata).
- Syntax highlighting for code files.
- Basic file and folder diff capabilities.
- Batch rename with preview.
- Symlink/junction/hard link support.
- File operation queue with pause/resume.
- Thumbnail cache.
- Color tags and custom metadata.
- Command palette.
- Duplicate finder.

### Phase 3 – Polish & Integration

- Performance tuning and background workers.
- Virtual scrolling for large directories.
- Refined theming, hotkey customization, layout profiles.
- Shell integration and command-line options.
- Plugin architecture foundations.
- Archive handling (create/extract).
- System tray support.
- Crash recovery.
- Enhanced diff with syntax highlighting.
- Split/merge files.
- Secure deletion.

### Phase 4 – Advanced Features (Future)

- Windows Search integration.
- Cloud storage indicators.
- Network drive optimization.
- Advanced grouping and filtering UI.
- Hex editor integration.
- FTP/SFTP support (phase 4+).
- Scripting/automation.
- Mobile device (MTP) support.
- Localization.

## 16. Technology Stack & Dependencies

### Core Libraries

- **UI Framework:** Dear ImGui (docking branch)
- **Graphics Backend:** DirectX 11 (initial) / DirectX 12 (future)
- **Windowing:** Win32 API
- **File System:** C++17/20 `<filesystem>` + Win32 API for advanced features
- **JSON:** nlohmann/json or simdjson
- **Threading:** C++11/14 threading, thread pools

### Optional/Phase 2+ Libraries

- **Image Loading:** stb_image, WIC (Windows Imaging Component)
- **Video Playback:** Windows Media Foundation or FFmpeg
- **Audio:** miniaudio or Windows Core Audio
- **Text Rendering:** FreeType (if custom fonts needed)
- **Syntax Highlighting:** TextMate grammar parser or similar
- **Diff Algorithm:** dtl (diff template library) or custom implementation
- **Archive Support:** libzip, zlib (built-in for .zip), optional 7zip integration
- **PDF Rendering:** MuPDF or PDFium (phase 2)
- **Hashing:** OpenSSL or Windows CryptoAPI
- **Regex:** std::regex or PCRE2

### Build System

- **Build Tool:** CMake (cross-platform, though Windows-focused)
- **Compiler:** MSVC 2019+ (primary), optional Clang/GCC support
- **Package Manager:** vcpkg for dependency management

## 17. Testing & Quality Assurance

- **Unit tests:** Core functionality (filesystem ops, path handling, sorting)
- **Integration tests:** File operations with real filesystem
- **UI tests:** Automated screenshot comparison (optional)
- **Performance benchmarks:** Large directory listing, search, copy operations
- **Memory profiling:** Valgrind/Dr. Memory equivalent for Windows
- **Crash reporting:** Optional crash dump generation and analysis

## 18. Documentation

- **User manual:** Online wiki or in-app help
- **Keyboard shortcuts reference:** Printable cheat sheet
- **Developer documentation:** Code architecture, subsystem documentation
- **API documentation:** For plugin developers (phase 2+)
- **Changelog:** Version history and release notes

---

This specification is intended as a living document. As Opacity evolves, new sections and details should be added to cover implementation decisions, third-party libraries (e.g., media decoders, diff engines), and platform-specific considerations.

## Appendix: Competitive Analysis

### Inspiration from Existing File Managers

- **Total Commander:** Dual-pane, keyboard-driven, extensive plugin system
- **Directory Opus:** Customizable columns, advanced filtering, scripting
- **XYplorer:** Tabbed browsing, color coding, dual pane, portable
- **Explorer++:** Open source, lightweight, tabbed interface
- **FreeCommander:** Dual-pane, archive support, built-in viewer
- **Windows File Explorer:** Familiar UI patterns, Quick Access, OneDrive integration

### Opacity's Differentiators

- Modern, GPU-accelerated UI with smooth animations
- Built-in diff tools (file and folder comparison)
- Rich preview system with video playback
- Fast, responsive C++ implementation
- Extensible architecture
- Power-user focused while maintaining approachability