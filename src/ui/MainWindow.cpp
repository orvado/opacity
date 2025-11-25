#include "opacity/ui/MainWindow.h"
#include "opacity/core/Logger.h"
#include "opacity/core/Path.h"

#include <imgui.h>

#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>

namespace opacity::ui
{

// Use alias to avoid any potential naming conflicts
using FsPath = opacity::core::Path;

MainWindow::MainWindow()
    : backend_(std::make_unique<ImGuiBackend>())
    , fs_manager_(std::make_unique<filesystem::FileSystemManager>())
    , preview_manager_(std::make_unique<preview::PreviewManager>())
    , search_engine_(std::make_unique<search::SearchEngine>())
    , keybind_manager_(std::make_unique<KeybindManager>())
    , current_theme_(std::make_unique<Theme>())
    , advanced_search_dialog_(std::make_unique<AdvancedSearchDialog>())
    , diff_viewer_(std::make_unique<DiffViewer>())
    , operation_queue_(std::make_unique<filesystem::OperationQueue>())
    , file_watch_(std::make_unique<filesystem::FileWatch>())
{
    // Create layout manager with shared_ptr version of fs_manager
    auto fs_shared = std::shared_ptr<filesystem::FileSystemManager>(
        fs_manager_.get(), [](filesystem::FileSystemManager*) {}); // Non-owning shared_ptr
    layout_manager_ = std::make_unique<LayoutManager>(fs_shared);
}

MainWindow::~MainWindow()
{
    Shutdown();
}

bool MainWindow::Initialize()
{
    SPDLOG_INFO("Initializing MainWindow...");

    if (!backend_->Initialize(L"Opacity - File Manager", 1400, 900))
    {
        SPDLOG_ERROR("Failed to initialize ImGui backend");
        return false;
    }

    // Initialize preview manager with D3D11 device
    preview_manager_->Initialize(backend_->GetDevice());

    // Set initial path to user's home directory
    current_path_ = fs_manager_->GetUserHomeDirectory();
    if (current_path_.empty())
    {
        current_path_ = "C:\\";
    }
    
    path_history_.push_back(current_path_);
    history_index_ = 0;

    // Load initial directory
    RefreshCurrentDirectory();

    // Initialize Phase 2 components
    current_theme_->Initialize();  // Initialize and apply default theme
    
    // Try to load saved keybinds
    keybind_manager_->LoadKeybinds("keybinds.json");
    
    // Start file watcher
    file_watch_->Start();
    
    // Set up file watch for current directory
    auto watch_callback = [this](const filesystem::FileChangeEvent& event) {
        // Refresh on any file change
        SPDLOG_DEBUG("File change detected: {} ({})", 
            event.path.String(), 
            static_cast<int>(event.type));
    };
    current_watch_handle_ = file_watch_->Watch(core::Path(current_path_), watch_callback);

    running_ = true;
    SPDLOG_INFO("MainWindow initialized successfully. Starting at: {}", current_path_);
    return true;
}

void MainWindow::Shutdown()
{
    if (!running_)
        return;
    
    running_ = false;
    
    // Cancel any ongoing search
    if (search_engine_ && search_engine_->IsSearching())
    {
        search_engine_->CancelSearch();
        search_engine_->WaitForCompletion();
    }
    
    // Stop Phase 2 components
    if (file_watch_)
    {
        file_watch_->Stop();
    }
    
    // Save keybinds
    if (keybind_manager_)
    {
        keybind_manager_->SaveKeybinds("keybinds.json");
    }
    
    // Release preview resources
    ReleaseCurrentPreview();
    
    backend_->Shutdown();
    SPDLOG_INFO("MainWindow shutdown complete");
}

void MainWindow::Run()
{
    SPDLOG_INFO("Entering main loop...");

    while (backend_->IsRunning() && running_)
    {
        if (!backend_->ProcessMessages())
            break;

        if (!backend_->BeginFrame())
            continue;

        // Create main dockspace
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar;
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("MainDockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(3);

        // Handle keyboard input
        HandleKeyboardInput();

        // Render UI components
        RenderMenuBar();
        
        // Main content area
        ImGui::BeginChild("ContentArea", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
        
        RenderToolbar();
        RenderAddressBar();
        
        // Split between file panel and preview
        float preview_width = show_preview_panel_ ? 300.0f : 0.0f;
        
        ImGui::BeginChild("FilePanel", ImVec2(-preview_width, 0), true);
        RenderFilePanel();
        ImGui::EndChild();

        if (show_preview_panel_)
        {
            ImGui::SameLine();
            ImGui::BeginChild("PreviewPanel", ImVec2(0, 0), true);
            RenderPreviewPanel();
            ImGui::EndChild();
        }

        ImGui::EndChild();
        
        RenderStatusBar();

        ImGui::End();
        
        // Render search results window if active
        RenderSearchResults();
        
        // Render Phase 2 dialogs
        if (advanced_search_dialog_)
        {
            advanced_search_dialog_->Render();
        }
        if (diff_viewer_)
        {
            diff_viewer_->Render();
        }
        
        // Render operation progress using OperationQueue's built-in UI
        if (show_operation_progress_ && operation_queue_)
        {
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Operations", &show_operation_progress_))
            {
                operation_queue_->RenderUI();
            }
            ImGui::End();
        }

        backend_->EndFrame();
    }

    SPDLOG_INFO("Exiting main loop");
}

void MainWindow::RenderMenuBar()
{
    if (ImGui::BeginMenuBar())
    {
        HandleFileMenu();
        HandleEditMenu();
        HandleViewMenu();
        HandleHelpMenu();
        ImGui::EndMenuBar();
    }
}

void MainWindow::HandleFileMenu()
{
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Folder", "Ctrl+Shift+N"))
            CreateNewFolder();
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Open", "Enter"))
            OpenSelectedItems();
        
        if (ImGui::MenuItem("Compare Files...", "Ctrl+D"))
        {
            // Get selected files for comparison
            std::vector<std::string> selected;
            for (size_t i = 0; i < current_items_.size() && i < selection_.size(); ++i)
            {
                if (selection_[i] && !current_items_[i].is_directory)
                {
                    selected.push_back(current_items_[i].full_path.String());
                }
            }
            if (selected.size() >= 2 && diff_viewer_)
            {
                diff_viewer_->CompareFiles(selected[0], selected[1]);
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            running_ = false;
        
        ImGui::EndMenu();
    }
}

void MainWindow::HandleEditMenu()
{
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Cut", "Ctrl+X"))
            CutSelectedItems();
        
        if (ImGui::MenuItem("Copy", "Ctrl+C"))
            CopySelectedItems();
        
        if (ImGui::MenuItem("Paste", "Ctrl+V"))
            PasteItems();
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Delete", "Delete"))
            DeleteSelectedItems();
        
        if (ImGui::MenuItem("Rename", "F2"))
            RenameSelectedItem();
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Select All", "Ctrl+A"))
            SelectAll();
        
        if (ImGui::MenuItem("Invert Selection"))
            InvertSelection();
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Advanced Search...", "Ctrl+Shift+F"))
        {
            if (advanced_search_dialog_)
            {
                advanced_search_dialog_->Show();
            }
        }
        
        ImGui::EndMenu();
    }
}

void MainWindow::HandleViewMenu()
{
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Refresh", "F5"))
            RefreshCurrentDirectory();
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("View Mode"))
        {
            if (ImGui::MenuItem("Details", nullptr, view_mode_ == 0))
                view_mode_ = 0;
            if (ImGui::MenuItem("Icons", nullptr, view_mode_ == 1))
                view_mode_ = 1;
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Icon Size"))
        {
            if (ImGui::MenuItem("Small", nullptr, icon_size_ == 0))
                icon_size_ = 0;
            if (ImGui::MenuItem("Medium", nullptr, icon_size_ == 1))
                icon_size_ = 1;
            if (ImGui::MenuItem("Large", nullptr, icon_size_ == 2))
                icon_size_ = 2;
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Show Hidden Files", "Ctrl+H", &show_hidden_files_))
        {
            RefreshCurrentDirectory();
        }
        
        if (ImGui::MenuItem("Preview Panel", "Ctrl+P", &show_preview_panel_))
        {
            // Toggle handled by checkbox
        }
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("Layout"))
        {
            if (layout_manager_)
            {
                auto current_layout = layout_manager_->GetLayout();
                if (ImGui::MenuItem("Single Pane", nullptr, current_layout == LayoutType::Single))
                    layout_manager_->SetLayout(LayoutType::Single);
                if (ImGui::MenuItem("Dual Pane (Vertical)", nullptr, current_layout == LayoutType::DualVertical))
                    layout_manager_->SetLayout(LayoutType::DualVertical);
                if (ImGui::MenuItem("Dual Pane (Horizontal)", nullptr, current_layout == LayoutType::DualHorizontal))
                    layout_manager_->SetLayout(LayoutType::DualHorizontal);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Theme"))
        {
            if (ImGui::MenuItem("Dark Theme"))
            {
                current_theme_->ApplyDarkTheme();
            }
            if (ImGui::MenuItem("Light Theme"))
            {
                current_theme_->ApplyLightTheme();
            }
            if (ImGui::MenuItem("High Contrast"))
            {
                current_theme_->ApplyHighContrastTheme();
            }
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("Operations...", nullptr, &show_operation_progress_))
        {
            // Toggle handled by checkbox
        }
        
        ImGui::EndMenu();
    }
}

void MainWindow::HandleHelpMenu()
{
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::BeginMenu("Keyboard Shortcuts"))
        {
            ImGui::TextUnformatted("Navigation:");
            ImGui::BulletText("Alt+Left       Back");
            ImGui::BulletText("Alt+Right      Forward");
            ImGui::BulletText("Alt+Up         Parent folder");
            ImGui::BulletText("Backspace      Parent folder");
            ImGui::BulletText("Enter          Open selected");
            ImGui::BulletText("F5             Refresh");
            
            ImGui::Separator();
            ImGui::TextUnformatted("File Operations:");
            ImGui::BulletText("Ctrl+C         Copy");
            ImGui::BulletText("Ctrl+X         Cut");
            ImGui::BulletText("Ctrl+V         Paste");
            ImGui::BulletText("Delete         Delete");
            ImGui::BulletText("F2             Rename");
            ImGui::BulletText("Ctrl+Shift+N   New Folder");
            
            ImGui::Separator();
            ImGui::TextUnformatted("Selection:");
            ImGui::BulletText("Ctrl+A         Select All");
            ImGui::BulletText("Up/Down        Navigate list");
            ImGui::BulletText("Shift+Up/Down  Extend selection");
            ImGui::BulletText("Home/End       First/Last item");
            
            ImGui::Separator();
            ImGui::TextUnformatted("View:");
            ImGui::BulletText("Ctrl+P         Toggle Preview");
            ImGui::BulletText("Ctrl+H         Toggle Hidden Files");
            ImGui::BulletText("Escape         Cancel/Clear");
            
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("About Opacity"))
        {
            // Show simple about info in a tooltip-style popup
            SPDLOG_INFO("Opacity File Manager v1.0.0");
        }
        
        ImGui::EndMenu();
    }
}

void MainWindow::RenderToolbar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    
    // Navigation buttons
    ImGui::BeginDisabled(history_index_ == 0);
    if (ImGui::Button("<##Back"))
        NavigateBack();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back (Alt+Left)");
    
    ImGui::SameLine();
    
    ImGui::BeginDisabled(history_index_ >= path_history_.size() - 1);
    if (ImGui::Button(">##Forward"))
        NavigateForward();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward (Alt+Right)");
    
    ImGui::SameLine();
    
    if (ImGui::Button("^##Up"))
        NavigateUp();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up (Alt+Up)");
    
    ImGui::SameLine();
    
    if (ImGui::Button("R##Refresh"))
        RefreshCurrentDirectory();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh (F5)");
    
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    
    // File operation buttons
    if (ImGui::Button("New Folder"))
        CreateNewFolder();
    
    ImGui::SameLine();
    
    if (ImGui::Button("Copy"))
        CopySelectedItems();
    
    ImGui::SameLine();
    
    if (ImGui::Button("Cut"))
        CutSelectedItems();
    
    ImGui::SameLine();
    
    if (ImGui::Button("Paste"))
        PasteItems();
    
    ImGui::SameLine();
    
    if (ImGui::Button("Delete"))
        DeleteSelectedItems();
    
    ImGui::PopStyleVar(2);
}

void MainWindow::RenderAddressBar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    
    // Path buffer for editing
    char path_buffer[MAX_PATH * 2];
    strncpy_s(path_buffer, current_path_.c_str(), sizeof(path_buffer) - 1);
    
    ImGui::SetNextItemWidth(-250);
    if (ImGui::InputText("##AddressBar", path_buffer, sizeof(path_buffer), 
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        FsPath new_path{std::string{path_buffer}};
        if (fs_manager_->IsDirectory(new_path))
        {
            NavigateTo(path_buffer);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Address Bar - Enter path and press Enter");
    
    ImGui::SameLine();
    
    // Search box with Enter to search
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputTextWithHint("##Search", "Search...", search_buffer_, sizeof(search_buffer_),
                                  ImGuiInputTextFlags_EnterReturnsTrue))
    {
        if (strlen(search_buffer_) > 0)
        {
            StartSearch();
        }
    }
    
    // Local filter still works on key input
    search_active_ = strlen(search_buffer_) > 0;
    
    ImGui::SameLine();
    
    // Search button
    if (ImGui::Button("Go"))
    {
        if (strlen(search_buffer_) > 0)
        {
            StartSearch();
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search files recursively (Enter)");
    
    ImGui::PopStyleVar();
    ImGui::Separator();
}

void MainWindow::RenderFilePanel()
{
    // Column headers for details view
    if (view_mode_ == 0) // Details view
    {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                               ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY;
        
        if (ImGui::BeginTable("FileList", 4, flags))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            // Handle sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs())
            {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
                    filesystem::SortColumn new_column;
                    switch (spec.ColumnIndex)
                    {
                    case 0: new_column = filesystem::SortColumn::Name; break;
                    case 1: new_column = filesystem::SortColumn::Size; break;
                    case 2: new_column = filesystem::SortColumn::Type; break;
                    case 3: new_column = filesystem::SortColumn::DateModified; break;
                    default: new_column = filesystem::SortColumn::Name; break;
                    }
                    
                    sort_column_ = new_column;
                    sort_direction_ = (spec.SortDirection == ImGuiSortDirection_Ascending) 
                        ? filesystem::SortDirection::Ascending 
                        : filesystem::SortDirection::Descending;
                    
                    // Re-sort the items
                    filesystem::FsItemComparator comparator(sort_column_, sort_direction_, true);
                    filesystem::FsItemUtils::Sort(current_items_, comparator);
                    
                    sort_specs->SpecsDirty = false;
                }
            }

            // Show error if any
            if (!last_error_.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", last_error_.c_str());
            }
            else
            {
                // Render each file item
                for (size_t i = 0; i < current_items_.size(); ++i)
                {
                    const auto& item = current_items_[i];
                    
                    // Apply search filter
                    if (search_active_)
                    {
                        std::string lower_name = item.name;
                        std::string lower_search = search_buffer_;
                        std::transform(lower_name.begin(), lower_name.end(), 
                                     lower_name.begin(), ::tolower);
                        std::transform(lower_search.begin(), lower_search.end(), 
                                     lower_search.begin(), ::tolower);
                        if (lower_name.find(lower_search) == std::string::npos)
                            continue;
                    }
                    
                    ImGui::TableNextRow();
                    
                    // Name column
                    ImGui::TableNextColumn();
                    const char* icon = item.is_directory ? "[DIR] " : "      ";
                    
                    // Ensure selection vector is sized correctly
                    if (selection_.size() != current_items_.size())
                    {
                        selection_.resize(current_items_.size(), false);
                    }
                    
                    bool selected = IsSelected(i);
                    ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | 
                                                     ImGuiSelectableFlags_AllowDoubleClick;
                    
                    std::string label = std::string(icon) + item.name + "##" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selected, sel_flags))
                    {
                        // Handle selection
                        bool ctrl_held = ImGui::GetIO().KeyCtrl;
                        bool shift_held = ImGui::GetIO().KeyShift;
                        
                        if (ctrl_held)
                        {
                            ToggleSelection(i);
                        }
                        else if (shift_held && selected_index_ >= 0)
                        {
                            // Range selection
                            ClearSelection();
                            size_t start = static_cast<size_t>(selected_index_);
                            size_t end = i;
                            if (start > end) std::swap(start, end);
                            for (size_t j = start; j <= end; ++j)
                                SetSelection(j, true);
                        }
                        else
                        {
                            ClearSelection();
                            SetSelection(i, true);
                        }
                        selected_index_ = static_cast<int>(i);
                    }
                    
                    // Double-click to open
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        if (item.is_directory)
                        {
                            NavigateTo(item.full_path.String());
                        }
                        else
                        {
                            // Open with default application
                            std::wstring wide_path;
                            std::string path_str = item.full_path.String();
                            int size_needed = MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), 
                                static_cast<int>(path_str.length()), nullptr, 0);
                            wide_path.resize(size_needed);
                            MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), 
                                static_cast<int>(path_str.length()), &wide_path[0], size_needed);
                            ShellExecuteW(NULL, L"open", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        }
                    }
                    
                    // Size column
                    ImGui::TableNextColumn();
                    if (!item.is_directory)
                    {
                        ImGui::TextUnformatted(item.GetFormattedSize().c_str());
                    }
                    
                    // Type column
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(item.GetTypeDescription().c_str());
                    
                    // Modified column
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(item.GetFormattedModifiedDate().c_str());
                }
            }

            ImGui::EndTable();
        }
    }
    else // Icon view
    {
        // TODO: Implement icon view
        ImGui::TextUnformatted("Icon view not yet implemented");
    }
}

void MainWindow::RenderStatusBar()
{
    ImGui::Separator();
    
    // Count selected items
    size_t selected_count = 0;
    for (bool s : selection_)
        if (s) selected_count++;
    
    if (selected_count > 0)
    {
        ImGui::Text("%zu item(s) selected", selected_count);
    }
    else
    {
        ImGui::Text("%zu folder(s), %zu file(s)", total_dirs_, total_files_);
    }
    
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::Text("Opacity v1.0.0");
}

void MainWindow::RenderPreviewPanel()
{
    ImGui::TextUnformatted("Preview");
    ImGui::Separator();
    
    // Show preview for selected item
    if (selected_index_ >= 0 && static_cast<size_t>(selected_index_) < current_items_.size())
    {
        const auto& item = current_items_[selected_index_];
        
        // Update preview if needed
        if (preview_file_path_ != item.full_path.String())
        {
            UpdatePreview();
        }
        
        ImGui::Text("Name: %s", item.name.c_str());
        ImGui::Text("Type: %s", item.GetTypeDescription().c_str());
        
        if (!item.is_directory)
        {
            ImGui::Text("Size: %s", item.GetFormattedSize().c_str());
        }
        
        ImGui::Text("Modified: %s", item.GetFormattedModifiedDate().c_str());
        ImGui::Text("Created: %s", item.GetFormattedCreatedDate().c_str());
        
        ImGui::Separator();
        
        // Show attributes
        if (item.IsHidden()) ImGui::TextUnformatted("[Hidden]");
        if (item.IsReadOnly()) ImGui::TextUnformatted("[Read-Only]");
        if (item.IsSystem()) ImGui::TextUnformatted("[System]");
        
        ImGui::Separator();
        
        // Show actual preview content
        if (!item.is_directory && current_preview_.type != preview::PreviewType::None)
        {
            if (!current_preview_.error_message.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", 
                    current_preview_.error_message.c_str());
            }
            else if (current_preview_.type == preview::PreviewType::Text)
            {
                // Text preview
                ImGui::TextUnformatted("Text Preview:");
                ImGui::BeginChild("TextPreviewScroll", ImVec2(0, 0), true, 
                    ImGuiWindowFlags_HorizontalScrollbar);
                
                const auto& text_data = current_preview_.text_preview;
                for (const auto& line : text_data.lines)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                
                if (text_data.truncated)
                {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), 
                        "[Truncated - showing %zu of %zu lines]", 
                        text_data.lines.size(), text_data.total_lines);
                }
                
                ImGui::EndChild();
            }
            else if (current_preview_.type == preview::PreviewType::Image)
            {
                // Image preview
                const auto& image_data = current_preview_.image_preview;
                
                ImGui::Text("Image: %dx%d, %d channels", 
                    image_data.info.width, image_data.info.height, image_data.info.channels);
                
                if (image_data.texture)
                {
                    // Calculate display size maintaining aspect ratio
                    float max_width = ImGui::GetContentRegionAvail().x;
                    float max_height = ImGui::GetContentRegionAvail().y - 20;
                    
                    float scale_x = max_width / static_cast<float>(image_data.info.width);
                    float scale_y = max_height / static_cast<float>(image_data.info.height);
                    float scale = (std::min)(scale_x, scale_y);
                    scale = (std::min)(scale, 1.0f); // Don't upscale
                    
                    float display_width = image_data.info.width * scale;
                    float display_height = image_data.info.height * scale;
                    
                    ImGui::Image(image_data.texture, ImVec2(display_width, display_height));
                }
            }
            else if (current_preview_.type == preview::PreviewType::Unsupported)
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
                    "Preview not available for this file type.");
            }
        }
    }
    else
    {
        preview_file_path_.clear();
        ImGui::TextWrapped("Select a file to preview its contents.");
    }
}

void MainWindow::RenderDrivesPanel()
{
    ImGui::TextUnformatted("Drives");
    ImGui::Separator();
    
    auto drives = fs_manager_->GetDrives();
    for (const auto& drive : drives)
    {
        if (!drive.is_ready) continue;
        
        std::string label = drive.drive_letter;
        if (!drive.volume_name.empty())
        {
            label += " (" + drive.volume_name + ")";
        }
        
        if (ImGui::Selectable(label.c_str()))
        {
            NavigateTo(drive.drive_letter + "\\");
        }
    }
}

void MainWindow::NavigateTo(const std::string& path)
{
    FsPath new_path{path};
    if (!fs_manager_->IsDirectory(new_path))
    {
        SPDLOG_WARN("Cannot navigate to invalid path: {}", path);
        return;
    }
    
    // Truncate forward history if we're not at the end
    if (history_index_ < path_history_.size() - 1)
    {
        path_history_.erase(path_history_.begin() + history_index_ + 1, path_history_.end());
    }
    
    current_path_ = path;
    path_history_.push_back(path);
    history_index_ = path_history_.size() - 1;
    
    // Clear selection
    ClearSelection();
    selected_index_ = -1;
    
    // Refresh directory contents
    RefreshCurrentDirectory();
    
    SPDLOG_DEBUG("Navigated to: {}", path);
}

void MainWindow::NavigateUp()
{
    FsPath parent = fs_manager_->GetParentDirectory(FsPath{current_path_});
    std::string parent_str = parent.String();
    
    if (!parent_str.empty() && parent_str != current_path_)
    {
        NavigateTo(parent_str);
    }
}

void MainWindow::NavigateBack()
{
    if (history_index_ > 0)
    {
        history_index_--;
        current_path_ = path_history_[history_index_];
        ClearSelection();
        selected_index_ = -1;
        RefreshCurrentDirectory();
        SPDLOG_DEBUG("Navigated back to: {}", current_path_);
    }
}

void MainWindow::NavigateForward()
{
    if (history_index_ < path_history_.size() - 1)
    {
        history_index_++;
        current_path_ = path_history_[history_index_];
        ClearSelection();
        selected_index_ = -1;
        RefreshCurrentDirectory();
        SPDLOG_DEBUG("Navigated forward to: {}", current_path_);
    }
}

void MainWindow::RefreshCurrentDirectory()
{
    filesystem::EnumerationOptions options;
    options.include_hidden = show_hidden_files_;
    options.include_system = false;
    options.sort_column = sort_column_;
    options.sort_direction = sort_direction_;
    options.folders_first = true;
    
    if (search_active_ && strlen(search_buffer_) > 0)
    {
        // Convert search to wildcard pattern
        options.filter_pattern = std::string("*") + search_buffer_ + "*";
    }
    
    auto contents = fs_manager_->EnumerateDirectory(FsPath{current_path_}, options);
    
    if (contents.success)
    {
        current_items_ = std::move(contents.items);
        total_files_ = contents.total_files;
        total_dirs_ = contents.total_directories;
        total_size_ = contents.total_size;
        last_error_.clear();
    }
    else
    {
        current_items_.clear();
        total_files_ = 0;
        total_dirs_ = 0;
        total_size_ = 0;
        last_error_ = contents.error_message;
    }
    
    // Resize selection vector
    selection_.resize(current_items_.size(), false);
    
    SPDLOG_DEBUG("Refreshed directory: {} ({} items)", current_path_, current_items_.size());
}

void MainWindow::OpenSelectedItems()
{
    for (size_t i = 0; i < current_items_.size(); ++i)
    {
        if (IsSelected(i))
        {
            const auto& item = current_items_[i];
            if (item.is_directory)
            {
                NavigateTo(item.full_path.String());
                break;  // Only navigate to first selected directory
            }
            else
            {
                // Open with default application
                std::wstring wide_path;
                std::string path_str = item.full_path.String();
                int size_needed = MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), 
                    static_cast<int>(path_str.length()), nullptr, 0);
                wide_path.resize(size_needed);
                MultiByteToWideChar(CP_UTF8, 0, path_str.c_str(), 
                    static_cast<int>(path_str.length()), &wide_path[0], size_needed);
                ShellExecuteW(NULL, L"open", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
    }
}

void MainWindow::CopySelectedItems()
{
    // TODO: Implement clipboard copy
    SPDLOG_DEBUG("Copy selected items");
}

void MainWindow::CutSelectedItems()
{
    // TODO: Implement clipboard cut
    SPDLOG_DEBUG("Cut selected items");
}

void MainWindow::PasteItems()
{
    // TODO: Implement clipboard paste
    SPDLOG_DEBUG("Paste items");
}

void MainWindow::DeleteSelectedItems()
{
    // Delete all selected items
    bool deleted_any = false;
    for (size_t i = 0; i < current_items_.size(); ++i)
    {
        if (IsSelected(i))
        {
            const auto& item = current_items_[i];
            bool success = fs_manager_->Delete(item.full_path, item.is_directory);
            if (success)
            {
                SPDLOG_INFO("Deleted: {}", item.full_path.String());
                deleted_any = true;
            }
            else
            {
                SPDLOG_WARN("Failed to delete: {}", item.full_path.String());
            }
        }
    }
    
    if (deleted_any)
    {
        RefreshCurrentDirectory();
    }
}

void MainWindow::RenameSelectedItem()
{
    // TODO: Implement in-place rename
    SPDLOG_DEBUG("Rename selected item");
}

void MainWindow::CreateNewFolder()
{
    std::string base_name = current_path_;
    if (!base_name.empty() && base_name.back() != '\\' && base_name.back() != '/')
    {
        base_name += "\\";
    }
    base_name += "New Folder";
    
    std::string new_folder = base_name;
    int counter = 1;
    while (fs_manager_->Exists(FsPath{new_folder}))
    {
        new_folder = base_name + " (" + std::to_string(counter++) + ")";
    }
    
    if (fs_manager_->MakeDirectory(FsPath{new_folder}))
    {
        SPDLOG_INFO("Created folder: {}", new_folder);
        RefreshCurrentDirectory();
    }
    else
    {
        SPDLOG_ERROR("Failed to create folder: {}", new_folder);
    }
}

void MainWindow::SelectAll()
{
    for (size_t i = 0; i < selection_.size(); ++i)
    {
        selection_[i] = true;
    }
}

void MainWindow::InvertSelection()
{
    for (size_t i = 0; i < selection_.size(); ++i)
    {
        selection_[i] = !selection_[i];
    }
}

void MainWindow::ClearSelection()
{
    for (size_t i = 0; i < selection_.size(); ++i)
    {
        selection_[i] = false;
    }
}

bool MainWindow::IsSelected(size_t index) const
{
    if (index < selection_.size())
    {
        return selection_[index];
    }
    return false;
}

void MainWindow::ToggleSelection(size_t index)
{
    if (index < selection_.size())
    {
        selection_[index] = !selection_[index];
    }
}

void MainWindow::SetSelection(size_t index, bool selected)
{
    if (index < selection_.size())
    {
        selection_[index] = selected;
    }
}

void MainWindow::UpdateSort(filesystem::SortColumn column)
{
    if (sort_column_ == column)
    {
        // Toggle direction
        sort_direction_ = (sort_direction_ == filesystem::SortDirection::Ascending) 
            ? filesystem::SortDirection::Descending 
            : filesystem::SortDirection::Ascending;
    }
    else
    {
        sort_column_ = column;
        sort_direction_ = filesystem::SortDirection::Ascending;
    }
    
    // Re-sort
    filesystem::FsItemComparator comparator(sort_column_, sort_direction_, true);
    filesystem::FsItemUtils::Sort(current_items_, comparator);
}

// ============================================================================
// Preview methods
// ============================================================================

void MainWindow::UpdatePreview()
{
    if (selected_index_ < 0 || static_cast<size_t>(selected_index_) >= current_items_.size())
    {
        ReleaseCurrentPreview();
        preview_file_path_.clear();
        return;
    }
    
    const auto& item = current_items_[selected_index_];
    
    // Skip directories
    if (item.is_directory)
    {
        ReleaseCurrentPreview();
        preview_file_path_.clear();
        return;
    }
    
    // Release previous preview
    ReleaseCurrentPreview();
    
    // Load new preview
    preview_file_path_ = item.full_path.String();
    current_preview_ = preview_manager_->LoadPreview(item.full_path);
    
    SPDLOG_DEBUG("Loaded preview for: {} (type={})", preview_file_path_, 
        static_cast<int>(current_preview_.type));
}

void MainWindow::ReleaseCurrentPreview()
{
    if (current_preview_.type != preview::PreviewType::None)
    {
        preview_manager_->ReleasePreview(current_preview_);
        current_preview_ = preview::PreviewData{};
    }
}

// ============================================================================
// Search methods
// ============================================================================

void MainWindow::StartSearch()
{
    if (search_engine_->IsSearching())
    {
        SPDLOG_DEBUG("Search already in progress");
        return;
    }
    
    std::string query = search_buffer_;
    if (query.empty())
    {
        return;
    }
    
    // Clear previous results
    {
        std::lock_guard<std::mutex> lock(search_results_mutex_);
        search_results_.clear();
        search_files_count_ = 0;
    }
    
    show_search_results_ = true;
    
    search::SearchOptions options;
    options.case_sensitive = false;
    options.recursive = true;
    options.include_hidden = show_hidden_files_;
    options.max_results = 500;
    
    SPDLOG_INFO("Starting search for '{}' in '{}'", query, current_path_);
    
    search_engine_->StartSearch(
        FsPath{current_path_},
        query,
        options,
        [this](const search::SearchResult& result) {
            OnSearchResult(result);
        },
        [this](size_t files_searched, size_t matches_found) {
            std::lock_guard<std::mutex> lock(search_results_mutex_);
            search_files_count_ = files_searched;
        }
    );
}

void MainWindow::CancelSearch()
{
    search_engine_->CancelSearch();
    SPDLOG_DEBUG("Search cancelled");
}

void MainWindow::OnSearchResult(const search::SearchResult& result)
{
    std::lock_guard<std::mutex> lock(search_results_mutex_);
    search_results_.push_back(result);
}

void MainWindow::RenderSearchResults()
{
    if (!show_search_results_)
    {
        return;
    }
    
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Search Results", &show_search_results_))
    {
        // Status bar
        {
            std::lock_guard<std::mutex> lock(search_results_mutex_);
            
            if (search_engine_->IsSearching())
            {
                ImGui::Text("Searching... (%zu files, %zu matches)", 
                    search_files_count_, search_results_.size());
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    CancelSearch();
                }
            }
            else
            {
                ImGui::Text("Found %zu matches", search_results_.size());
            }
        }
        
        ImGui::Separator();
        
        // Results list
        if (ImGui::BeginTable("SearchResultsTable", 3, 
            ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | 
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            
            std::lock_guard<std::mutex> lock(search_results_mutex_);
            
            for (size_t i = 0; i < search_results_.size(); ++i)
            {
                const auto& result = search_results_[i];
                
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                const char* icon = result.item.is_directory ? "[DIR] " : "";
                std::string label = std::string(icon) + result.item.name + "##sr" + std::to_string(i);
                
                if (ImGui::Selectable(label.c_str(), false, 
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        if (result.item.is_directory)
                        {
                            NavigateTo(result.item.full_path.String());
                            show_search_results_ = false;
                        }
                        else
                        {
                            // Navigate to containing folder and select the file
                            std::string parent = fs_manager_->GetParentDirectory(result.item.full_path).String();
                            if (!parent.empty())
                            {
                                NavigateTo(parent);
                                show_search_results_ = false;
                            }
                        }
                    }
                }
                
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(result.item.full_path.String().c_str());
                
                ImGui::TableNextColumn();
                if (!result.item.is_directory)
                {
                    ImGui::TextUnformatted(result.item.GetFormattedSize().c_str());
                }
            }
            
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// ============================================================================
// Keyboard Input Handling
// ============================================================================

void MainWindow::HandleKeyboardInput()
{
    // Don't process shortcuts if an input field is active
    if (ImGui::GetIO().WantTextInput)
    {
        return;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    
    // Navigation shortcuts
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
        NavigateBack();
    }
    else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
        NavigateForward();
    }
    else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        NavigateUp();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F5))
    {
        RefreshCurrentDirectory();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !io.KeyCtrl && !io.KeyShift)
    {
        NavigateUp();
    }
    
    // File operations
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        CopySelectedItems();
    }
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X))
    {
        CutSelectedItems();
    }
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        PasteItems();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        DeleteSelectedItems();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_F2))
    {
        RenameSelectedItem();
    }
    else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_N))
    {
        CreateNewFolder();
    }
    
    // Selection
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
    {
        SelectAll();
    }
    
    // Arrow key navigation in file list
    if (!current_items_.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            int new_index = selected_index_ + 1;
            if (new_index < static_cast<int>(current_items_.size()))
            {
                if (!io.KeyShift)
                {
                    ClearSelection();
                }
                selected_index_ = new_index;
                SetSelection(static_cast<size_t>(new_index), true);
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            int new_index = selected_index_ - 1;
            if (new_index >= 0)
            {
                if (!io.KeyShift)
                {
                    ClearSelection();
                }
                selected_index_ = new_index;
                SetSelection(static_cast<size_t>(new_index), true);
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            if (!io.KeyShift)
            {
                ClearSelection();
            }
            selected_index_ = 0;
            SetSelection(0, true);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_End))
        {
            if (!io.KeyShift)
            {
                ClearSelection();
            }
            selected_index_ = static_cast<int>(current_items_.size()) - 1;
            SetSelection(static_cast<size_t>(selected_index_), true);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter))
        {
            OpenSelectedItems();
        }
    }
    
    // Toggle preview panel
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P))
    {
        show_preview_panel_ = !show_preview_panel_;
    }
    
    // Toggle hidden files
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H))
    {
        show_hidden_files_ = !show_hidden_files_;
        RefreshCurrentDirectory();
    }
    
    // Escape to cancel search or clear selection
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        if (show_search_results_)
        {
            if (search_engine_->IsSearching())
            {
                CancelSearch();
            }
            else
            {
                show_search_results_ = false;
            }
        }
        else
        {
            ClearSelection();
            selected_index_ = -1;
        }
    }
}

} // namespace opacity::ui
