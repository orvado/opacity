# Opacity - Phased Development Plan

## Development Philosophy

**GUI-First Approach**: All functionality is accessible through mouse and keyboard interactions within the application. No REST API services are required or planned. Every feature must be implementable through direct UI interactions, keyboard shortcuts, and menu commands.

**Incremental Delivery**: Each phase produces a fully functional, testable application that delivers real value to users. Phases build upon each other while maintaining stability and code quality.

**Modern C++ Practices**: Use C++17/20 features, RAII, smart pointers, and modern design patterns throughout the codebase.

---

## Phase 1: Foundation & Basic File Explorer (4-6 weeks)

### Objectives
Create a minimal but complete file explorer with core navigation and basic file operations. This phase establishes the technical foundation and basic user workflow.

### Core Deliverables

#### 1.1 Application Infrastructure
- **Win32 + ImGui Setup**
  - Main window creation with DirectX 11 backend
  - ImGui context initialization and docking branch integration
  - Basic main loop with message pump and frame rendering
  - Error handling and logging framework
- **Core Architecture**
  - Basic configuration system (JSON-based settings)
  - Logging system with file output and verbosity levels
  - Path abstraction layer wrapping `std::filesystem::path`
  - Basic thread pool for background operations

#### 1.2 Basic File System Interface
- **File System Core**
  - `FsItem` data model with basic properties (name, path, size, timestamps, attributes)
  - Directory listing with async enumeration
  - Drive enumeration and mounting point detection
  - Basic file metadata fetching
- **Navigation**
  - Current directory state management
  - Navigation history (back/forward/up)
  - Address bar with breadcrumb navigation
  - Basic path input and validation

#### 1.3 User Interface Foundation
- **Main Window Layout**
  - Single pane layout with tab support
  - Basic toolbar with common operations
  - Status bar showing item count and selected items info
  - Basic menu bar (File, Edit, View, Help)
- **File List Display**
  - Details view with basic columns (Name, Size, Type, Modified Date)
  - Icon view with small/medium/large sizes
  - Basic sorting (name, size, date, type)
  - Keyboard navigation (arrow keys, page up/down, home/end)

#### 1.4 Basic File Operations
- **Core Operations**
  - Open files with default system application
  - Copy/paste with progress dialog
  - Move operations with progress tracking
  - Delete (recycle bin) with confirmation
  - Rename with in-place editing
  - Create new folder and basic text files
- **Selection System**
  - Single and multi-selection (Ctrl+click, Shift+click)
  - Select all/invert selection
  - Basic visual feedback for selected items

#### 1.5 Basic Search & Filtering
- **Simple Search**
  - Real-time search box for current directory
  - Name-based search with substring matching
  - Extension filtering
  - Basic wildcard support (*.txt, etc.)
- **Quick Filters**
  - Show/hide hidden files
  - Filter by file type groups

#### 1.6 Basic Preview System
- **Image Previews**
  - PNG, JPEG, BMP, GIF support via stb_image
  - Basic zoom and pan controls
  - Thumbnail generation in background
- **Text Previews**
  - Plain text file viewer with basic syntax highlighting
  - Line numbers and word wrap
  - Size limits for large files

### Technical Implementation Details

#### GUI Components
- All functionality accessible through:
  - Menu bar commands
  - Toolbar buttons
  - Context menus (right-click)
  - Keyboard shortcuts (F2 rename, Delete, Ctrl+C/X/V, etc.)
  - Direct interaction (double-click to open, drag to select)

#### Key Architecture Decisions
- **No external services**: All functionality implemented locally
- **Async operations**: File operations use background threads with progress dialogs
- **Memory management**: Smart pointers throughout, RAII for resource management
- **Error handling**: Non-blocking error dialogs with detailed messages

### Testing & Validation
- Manual testing of all file operations
- Performance testing with directories containing 1000+ files
- Memory leak detection during prolonged use
- Basic stress testing of file operations

---

## Phase 2: Enhanced Interface & Advanced Operations (5-7 weeks)

### Objectives
Expand the user interface with dual-pane support, advanced file operations, and enhanced preview capabilities. Focus on power-user features and workflow efficiency.

### Core Deliverables

#### 2.1 Advanced Interface Layouts
- **Multi-Pane Support**
  - Dual-pane layout (horizontal and vertical splits)
  - Pane synchronization options
  - Independent navigation per pane
  - Hotkey-based pane switching (F6, Tab)
- **Enhanced Tab Management**
  - Tab duplication and pinning
  - Tab reordering with drag-and-drop
  - Reopen closed tabs (Ctrl+Shift+T)
  - Tab color coding and custom names

#### 2.2 Advanced File Operations
- **Batch Operations**
  - Multi-file copy/move with conflict resolution
  - Operation queue with pause/resume capability
  - Background operation progress indicators
  - Speed throttling for I/O intensive operations
- **Advanced File Operations**
  - Hard links, symbolic links, and junctions creation
  - File property editing (attributes, timestamps)
  - Basic file integrity verification (checksums)
  - Secure deletion (single-pass overwrite)

#### 2.3 Enhanced Search & Filtering
- **Advanced Search Dialog**
  - Multiple search criteria (size, date, attributes)
  - Search scope selection (current dir, subfolders, drives)
  - Saved search configurations
  - Search result management (virtual folders)
- **Advanced Filtering**
  - Column-based filters (size ranges, date ranges)
  - Chained filters with AND/OR logic
  - Filter presets for common use cases
  - Negative filters (exclude patterns)

#### 2.4 Rich Preview System
- **Media Previews**
  - Video playback via Windows Media Foundation
  - Audio playback with basic controls
  - Extended image format support (TIFF, WebP, ICO)
  - PDF preview integration
- **Enhanced Text Preview**
  - Syntax highlighting for 20+ programming languages
  - Hex viewer for binary files
  - Basic code folding and line wrapping
  - Character encoding detection

#### 2.5 Basic Diff Capabilities
- **File Comparison**
  - Side-by-side text diff viewer
  - Binary file comparison (hex view)
  - Basic diff options (ignore whitespace, case sensitivity)
  - Navigation between differences
- **Comparison Integration**
  - Context menu "Compare with..." functionality
  - Mark for comparison workflow
  - Basic result export functionality

#### 2.6 Customization & Settings
- **Theming System**
  - Light and dark theme support
  - Custom accent colors
  - Font size and family configuration
  - Icon size presets
- **Keyboard Customization**
  - Rebindable hotkeys with export/import
  - Context-sensitive shortcut help
  - Command palette prototype (F1)

### Technical Implementation Details

#### GUI Enhancements
- **Advanced Context Menus**
  - Dynamic menu items based on selection
  - Custom actions configuration
  - Menu customization interface
- **Drag-and-Drop System**
  - Internal drag between panes
  - External drag from Windows Explorer
  - Visual feedback during operations
  - Modifier key handling (Ctrl/Shift/Alt)

#### Performance Optimizations
- **Virtual Scrolling**
  - Efficient rendering of large directories (10,000+ items)
  - Background thumbnail generation with priority queue
  - Lazy loading of file metadata
- **Caching System**
  - Thumbnail cache with LRU eviction
  - Directory listing cache with TTL
  - Search result caching

### Testing & Validation
- Performance testing with 50,000+ file directories
- Memory usage profiling during extended operations
- Multi-threading stress testing
- User workflow validation (common file management tasks)

---

## Phase 3: Power Features & Integration (6-8 weeks)

### Objectives
Implement advanced power-user features, system integration, and polish the user experience with animations, advanced customization, and comprehensive error handling.

### Core Deliverables

#### 3.1 Advanced Diff & Comparison
- **Folder Comparison**
  - Recursive folder comparison
  - Multiple comparison modes (name, size, hash)
  - Visual diff result presentation
  - Sync operations from comparison view
- **Enhanced File Diff**
  - Three-way merge capability
  - Advanced syntax highlighting in diff
  - Diff export to various formats
  - Integration with external diff tools

#### 3.2 Archive Management
- **Archive Integration**
  - ZIP file browsing as virtual folders
  - Archive creation from selection
  - Extract with path preservation
  - Multi-volume archive support
- **Archive Previews**
  - Archive content preview panel
  - Compression ratio display
  - Archive metadata extraction

#### 3.3 Batch Operations & Automation
- **Advanced Batch Rename**
  - Pattern-based renaming with preview
  - Numbering with custom padding
  - EXIF data integration for images
  - Regex support for complex patterns
- **Duplicate File Finder**
  - Hash-based duplicate detection
  - Similar file detection (content-based)
  - Duplicate management actions
  - Smart selection algorithms

#### 3.4 System Integration
- **Shell Integration**
  - Windows Explorer context menu integration
  - "Open with Opacity" registration
  - Send To menu items
  - Default file manager options
- **Command Line Interface**
  - Command line argument parsing
  - Single-instance IPC communication
  - Scriptable operations via command line
  - Windows Terminal integration

#### 3.5 Advanced UI Features
- **Command Palette**
  - Fuzzy search for all commands
  - Keyboard-driven workflow
  - Custom command creation
  - Plugin command integration (future)
- **Advanced Customization**
  - Layout profiles (save/restore window arrangements)
  - Custom toolbar configuration
  - Column presets and management
  - User-defined file type groups

#### 3.6 Monitoring & Real-time Updates
- **File System Monitoring**
  - Real-time directory change detection
  - Auto-refresh with configurable behavior
  - Conflict resolution for concurrent modifications
  - Change notifications and logging
- **Background Operations**
  - Comprehensive operation queue management
  - Background thumbnail generation optimization
  - Progress notification system
  - Operation history and recovery

### Technical Implementation Details

#### Advanced GUI Features
- **Animations & Transitions**
  - Smooth view mode transitions
  - Progress animations
  - Notification system with toast messages
  - Hover effects and micro-interactions
- **Advanced Keyboard Navigation**
  - Vi-like navigation mode (optional)
  - Custom keyboard shortcut profiles
  - Modal dialog navigation
  - Global hotkey support

#### Performance & Scalability
- **Advanced Caching**
  - Multi-level caching strategy
  - Predictive thumbnail generation
  - Memory-mapped file operations
  - Background data preloading
- **Memory Management**
  - Custom memory pools for frequent allocations
  - Smart pointer optimization
  - Resource cleanup on component destruction
  - Memory usage monitoring and alerts

### Testing & Validation
- Integration testing with Windows shell
- Performance benchmarking against Windows Explorer
- Stress testing with concurrent operations
- User experience testing with power users
- Accessibility testing (keyboard navigation, high contrast)

---

## Phase 4: Polish, Extensibility & Advanced Features (4-6 weeks)

### Objectives
Complete the application with advanced features, extensible architecture, comprehensive testing, and documentation. Focus on production readiness and future extensibility.

### Core Deliverables

#### 4.1 Plugin Architecture Foundation
- **Plugin System**
  - DLL-based plugin loading infrastructure
  - Plugin API documentation and examples
  - Plugin management interface
  - Security sandboxing for plugins
- **Core Plugin Points**
  - Custom preview handlers
  - Extended file operation providers
  - Custom search providers
  - UI extension points

#### 4.2 Advanced Preview & Media
- **Extended Media Support**
  - HEIF/HEIC image format support
  - Advanced video codec support via FFmpeg
  - Audio visualization and spectrum analysis
  - 3D model preview (basic)
- **Document Integration**
  - Office document preview via Windows handlers
  - Source code preview with advanced features
  - Markdown rendering with preview
  - Database file preview (basic)

#### 4.3 Network & Cloud Features
- **Network Storage**
  - Network drive optimization
  - Basic FTP/SFTP support (read-only)
  - UNC path handling improvements
  - Network operation timeout management
- **Cloud Integration**
  - OneDrive sync status indicators
  - Basic cloud storage provider detection
  - Cloud file offline status display

#### 4.4 Advanced Search & Indexing
- **Search Indexing**
  - Optional content indexing service
  - Fast search via built-in index
  - Index management and optimization
  - Windows Search integration (optional)
- **Advanced Search Features**
  - Full-text content search with highlighting
  - Regular expression search
  - Search result history and bookmarks
  - Advanced query builder

#### 4.5 System Tray & Background Features
- **System Tray Integration**
  - Minimize to tray functionality
  - Quick actions from tray menu
  - Background operation status
  - Tray notifications
- **Background Services**
  - Automatic operation scheduling
  - File system monitoring service
  - Background thumbnail generation service
  - Crash recovery and auto-save

#### 4.6 Comprehensive Testing & Documentation
- **Testing Suite**
  - Automated unit tests for core components
  - Integration test suite
  - Performance regression tests
  - Memory leak detection suite
- **Documentation**
  - Complete user manual with tutorials
  - Keyboard shortcut reference card
  - Developer documentation
  - Plugin development guide
- **Build & Distribution**
  - Automated build pipeline
  - Installer creation
  - Update mechanism foundation
  - Portable version support

### Technical Implementation Details

#### Advanced Architecture
- **Modular Design**
  - Clear separation between UI and business logic
  - Dependency injection system
  - Event-driven architecture
  - Comprehensive error boundary system
- **Performance Monitoring**
  - Built-in performance profiling
  - Memory usage tracking
  - Operation timing metrics
  - User behavior analytics (opt-in)

#### Security & Reliability
- **Security Features**
  - Code signing for executable and plugins
  - Plugin security validation
  - Secure temporary file handling
  - Protection against malicious file paths
- **Reliability Features**
  - Comprehensive error recovery
  - Crash reporting system
  - Automatic backup of settings
  - Graceful degradation for missing dependencies

### Testing & Validation
- End-to-end user workflow testing
- Plugin development validation
- Performance regression testing
- Security audit and penetration testing
- Accessibility compliance testing
- Multi-language support validation (if implemented)

---

## Development Guidelines & Best Practices

### Code Quality Standards
- **C++ Standards**: Use C++17 features, C++20 where beneficial
- **Design Patterns**: Apply appropriate patterns (Factory, Observer, Strategy, etc.)
- **Memory Management**: RAII, smart pointers, avoid raw pointers
- **Error Handling**: Comprehensive exception handling with user-friendly messages
- **Code Organization**: Clear separation of concerns, modular design

### GUI Development Principles
- **Responsive Design**: All operations must show progress and remain cancellable
- **Keyboard First**: Every feature accessible via keyboard, mouse support secondary
- **Consistent UX**: Follow Windows UI guidelines where appropriate
- **Visual Feedback**: Immediate response to all user interactions
- **Accessibility**: Support for high contrast, screen readers, keyboard navigation

### Performance Requirements
- **Startup Time**: < 1 second on SSD for cold start
- **Directory Loading**: Handle 10,000+ files without blocking UI
- **Memory Usage**: < 150MB baseline (excluding caches)
- **Frame Rate**: Maintain 60 FPS UI rendering
- **Operations**: Show progress for operations > 300ms

### Testing Strategy
- **Unit Tests**: Core logic, file operations, data models
- **Integration Tests**: File system operations, UI workflows
- **Performance Tests**: Large directories, memory usage, startup time
- **User Testing**: Real-world workflow validation
- **Automated Testing**: CI/CD pipeline with automated test suite

### Version Control & Collaboration
- **Branch Strategy**: Feature branches, regular integration
- **Code Review**: All changes require review
- **Documentation**: Code comments, API documentation, user docs
- **Issue Tracking**: Comprehensive bug tracking and feature planning

---

## Success Criteria for Each Phase

### Phase 1 Success Criteria
- [ ] Application launches and displays files in current directory
- [ ] Basic file operations (copy, move, delete, rename) work correctly
- [ ] Navigation between folders functions properly
- [ ] Basic search and filtering operate as expected
- [ ] Image and text previews display correctly
- [ ] Application remains stable during normal operations

### Phase 2 Success Criteria
- [ ] Dual-pane layout with independent navigation
- [ ] Advanced search with multiple criteria works
- [ ] Media previews (video, audio) function correctly
- [ ] Basic file comparison features operate as designed
- [ ] Customization options (themes, hotkeys) work properly
- [ ] Performance meets baseline requirements (10,000+ files)

### Phase 3 Success Criteria
- [ ] Folder comparison and sync operations work correctly
- [ ] Archive browsing and creation functions properly
- [ ] Batch rename and duplicate finder operate as expected
- [ ] System integration (shell, command line) works correctly
- [ ] Real-time file system monitoring functions properly
- [ ] Advanced UI features (command palette) work smoothly

### Phase 4 Success Criteria
- [ ] Plugin system loads and manages extensions correctly
- [ ] Advanced media and document previews work properly
- [ ] Network and cloud features function as designed
- [ ] Comprehensive testing suite passes
- [ ] Documentation is complete and accurate
- [ ] Application meets all performance and stability targets

---

This phased development plan ensures incremental delivery of a fully functional file manager while maintaining code quality, performance, and user experience throughout the development process. Each phase builds upon the previous one, creating a robust foundation for advanced features while delivering real value at every stage.
