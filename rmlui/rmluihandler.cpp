/*
 * RmlUI Handler Implementation
 */

#ifdef LUMINOVEAU_WITH_RMLUI

#include "rmluihandler.h"
#include "rmluibackend.h"
#include "log/loghandler.h"
#include "window/windowhandler.h"
#include "renderer/rendererhandler.h"
#include "file/filehandler.h"
#include "assethandler/assethandler.h"
#include <RmlUi/Debugger.h>

namespace RmlUI {

// ============================================================================
// INTERNAL STATE
// ============================================================================

struct State {
    bool initialized = false;
    Rml::Context* main_context = nullptr;
    std::unordered_map<std::string, Rml::Context*> contexts;
    std::unordered_map<std::string, Rml::ElementDocument*> documents;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<EventCallback>>> event_listeners;
    std::unordered_map<std::string, std::string> debug_values;
    bool debug_overlay_enabled = false;
};

static State g_state;

// ============================================================================
// FONT LOADING HELPERS
// ============================================================================

bool LoadFontFromFile(const std::string& filepath, bool fallback) {
    if (!g_state.initialized) {
        LOG_ERROR("RmlUI not initialized");
        return false;
    }
    
    bool success = Rml::LoadFontFace(filepath, fallback);
    if (success) {
        LOG_INFO("Loaded font: {}", filepath);
    } else {
        LOG_ERROR("Failed to load font: {}", filepath);
    }
    return success;
}

bool LoadFontFromMemory(const unsigned char* data, size_t data_length, 
                       const std::string& family, Rml::Style::FontStyle style, 
                       Rml::Style::FontWeight weight, bool fallback) {
    if (!g_state.initialized) {
        LOG_ERROR("RmlUI not initialized");
        return false;
    }
    
    // Create a Span from the data
    Rml::Span<const Rml::byte> font_data(reinterpret_cast<const Rml::byte*>(data), data_length);
    
    bool success = Rml::LoadFontFace(font_data, family, style, weight, fallback);
    if (success) {
        LOG_INFO("Loaded font from memory: {}", family);
    } else {
        LOG_ERROR("Failed to load font from memory: {}", family);
    }
    return success;
}

bool LoadDefaultFont() {
    // Access the embedded font from AssetHandler
    auto [font_data, font_size] = AssetHandler::GetEmbeddedFontData();
    return LoadFontFromMemory(
        font_data,
        font_size,
        "DroidSansMono", 
        Rml::Style::FontStyle::Normal, 
        Rml::Style::FontWeight::Normal,
        true  // fallback=true makes this the default font
    );
}

// ============================================================================
// CUSTOM EVENT LISTENER CLASS
// ============================================================================

class CustomEventListener : public Rml::EventListener {
public:
    CustomEventListener(EventCallback callback) : callback_(callback) {}
    
    void ProcessEvent(Rml::Event& event) override {
        if (callback_) {
            callback_(event);
        }
    }

private:
    EventCallback callback_;
};

// ============================================================================
// LIFECYCLE
// ============================================================================

void Init() {
    if (g_state.initialized) {
        LOG_WARNING("RmlUI already initialized");
        return;
    }

    // Get window info from Window namespace
    SDL_Window* window = Window::GetWindow();
    if (!window) {
        LOG_ERROR("RmlUI::Init() failed: Window not initialized. Call Window::InitWindow() first.");
        return;
    }

    // Get GPU device from Renderer
    SDL_GPUDevice* device = Renderer::GetDevice();
    
    // Initialize backend
    if (!Backend::Initialize(device, window)) {
        LOG_ERROR("Failed to initialize RmlUI backend");
        return;
    }

    // Initialize RmlUi core
    if (!Rml::Initialise()) {
        LOG_ERROR("Failed to initialize RmlUi core");
        Backend::Shutdown();
        return;
    }

    // Create main context
    vf2d window_size = Window::GetSize();
    g_state.main_context = Rml::CreateContext("main", 
        Rml::Vector2i(static_cast<int>(window_size.x), static_cast<int>(window_size.y)));
    
    if (!g_state.main_context) {
        LOG_ERROR("Failed to create main RmlUI context");
        Rml::Shutdown();
        Backend::Shutdown();
        return;
    }

    g_state.contexts["main"] = g_state.main_context;
    
    // Note: Users should load fonts manually using RmlUI::LoadFontFromFile()
    // or RmlUI::LoadFontFromMemory() after initialization
    // RmlUI will show warnings if no fonts are loaded, but will still function
    
    g_state.initialized = true;

    LOG_INFO("RmlUI initialized successfully - remember to load fonts for text rendering");
}

void Shutdown() {
    if (!g_state.initialized) {
        return;
    }

    // Clean up all event listeners
    for (auto& [doc_path, listeners] : g_state.event_listeners) {
        listeners.clear();
    }
    g_state.event_listeners.clear();

    // Unload all documents
    for (auto& [path, doc] : g_state.documents) {
        if (doc && doc->GetContext()) {
            doc->Close();
        }
    }
    g_state.documents.clear();

    // Remove all contexts
    for (auto& [name, context] : g_state.contexts) {
        if (context) {
            Rml::RemoveContext(name);
        }
    }
    g_state.contexts.clear();
    g_state.main_context = nullptr;

    // Shutdown RmlUi core
    Rml::Shutdown();

    // Shutdown backend
    Backend::Shutdown();

    g_state.initialized = false;
    LOG_INFO("RmlUI shut down");
}

// ============================================================================
// CONTEXT MANAGEMENT
// ============================================================================

Rml::Context* GetContext() {
    return g_state.main_context;
}

Rml::Context* CreateContext(const std::string& name, vf2d size) {
    if (!g_state.initialized) {
        LOG_ERROR("RmlUI not initialized");
        return nullptr;
    }

    if (g_state.contexts.find(name) != g_state.contexts.end()) {
        LOG_WARNING("Context '{}' already exists", name);
        return g_state.contexts[name];
    }

    Rml::Context* context = Rml::CreateContext(name, 
        Rml::Vector2i(static_cast<int>(size.x), static_cast<int>(size.y)));
    
    if (context) {
        g_state.contexts[name] = context;
        LOG_INFO("Created RmlUI context: {}", name);
    }

    return context;
}

Rml::Context* GetContextByName(const std::string& name) {
    auto it = g_state.contexts.find(name);
    return (it != g_state.contexts.end()) ? it->second : nullptr;
}

// ============================================================================
// DOCUMENT MANAGEMENT
// ============================================================================

Rml::ElementDocument* LoadDocument(const std::string& filepath) {
    if (!g_state.initialized) {
        LOG_ERROR("RmlUI not initialized");
        return nullptr;
    }

    // Check if already loaded
    auto it = g_state.documents.find(filepath);
    if (it != g_state.documents.end()) {
        LOG_INFO("Document '{}' already loaded", filepath);
        return it->second;
    }

    // Load the document
    Rml::ElementDocument* document = g_state.main_context->LoadDocument(filepath);
    
    if (!document) {
        LOG_ERROR("Failed to load RML document: {}", filepath);
        return nullptr;
    }

    g_state.documents[filepath] = document;
    LOG_INFO("Loaded RML document: {}", filepath);

    return document;
}

Rml::ElementDocument* GetDocument(const std::string& filepath) {
    auto it = g_state.documents.find(filepath);
    return (it != g_state.documents.end()) ? it->second : nullptr;
}

void ShowDocument(const std::string& filepath) {
    Rml::ElementDocument* doc = GetDocument(filepath);
    if (!doc) {
        doc = LoadDocument(filepath);
    }
    
    if (doc) {
        doc->Show();
    }
}

void HideDocument(const std::string& filepath) {
    Rml::ElementDocument* doc = GetDocument(filepath);
    if (doc) {
        doc->Hide();
    }
}

void ToggleDocument(const std::string& filepath) {
    Rml::ElementDocument* doc = GetDocument(filepath);
    if (!doc) {
        doc = LoadDocument(filepath);
        if (doc) {
            doc->Show();
        }
        return;
    }
    
    if (doc->IsVisible()) {
        doc->Hide();
    } else {
        doc->Show();
    }
}

bool IsDocumentVisible(const std::string& filepath) {
    Rml::ElementDocument* doc = GetDocument(filepath);
    return doc ? doc->IsVisible() : false;
}

void UnloadDocument(const std::string& filepath) {
    auto it = g_state.documents.find(filepath);
    if (it != g_state.documents.end()) {
        if (it->second && it->second->GetContext()) {
            it->second->Close();
        }
        g_state.documents.erase(it);
        
        // Clean up associated event listeners
        g_state.event_listeners.erase(filepath);
        
        LOG_INFO("Unloaded RML document: {}", filepath);
    }
}

void CloseDocument(const std::string& filepath) {
    UnloadDocument(filepath);
}

// ============================================================================
// RENDERING
// ============================================================================

void Render() {
    if (!g_state.initialized) {
        return;
    }

    // Update and render all contexts
    for (auto& [name, context] : g_state.contexts) {
        if (context) {
            context->Update();
            context->Render();
        }
    }
}

void Update() {
    if (!g_state.initialized) {
        return;
    }

    // Update all contexts without rendering
    for (auto& [name, context] : g_state.contexts) {
        if (context) {
            context->Update();
        }
    }
}

// ============================================================================
// ELEMENT MANIPULATION
// ============================================================================

void SetElementText(const std::string& document_path, const std::string& element_id, const std::string& text) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        element->SetInnerRML(text);
    }
}

void SetElementValue(const std::string& document_path, const std::string& element_id, const std::string& value) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        element->SetAttribute("value", value);
    }
}

std::string GetElementText(const std::string& document_path, const std::string& element_id) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        return element->GetInnerRML();
    }
    return "";
}

std::string GetElementValue(const std::string& document_path, const std::string& element_id) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        auto variant = element->GetAttribute("value");
        if (variant) {
            return variant->Get<Rml::String>();
        }
    }
    return "";
}

Rml::Element* GetElement(const std::string& document_path, const std::string& element_id) {
    Rml::ElementDocument* doc = GetDocument(document_path);
    if (!doc) {
        LOG_WARNING("Document not found: {}", document_path);
        return nullptr;
    }
    
    Rml::Element* element = doc->GetElementById(element_id);
    if (!element) {
        LOG_WARNING("Element '{}' not found in document '{}'", element_id, document_path);
    }
    
    return element;
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

void RegisterEventListener(const std::string& document_path, const std::string& element_id, 
                          const std::string& event_type, EventCallback callback) {
    if (!callback) {
        LOG_WARNING("Null callback provided for event listener");
        return;
    }

    Rml::Element* element = GetElement(document_path, element_id);
    if (!element) {
        return;
    }

    // Create and attach the event listener
    auto* listener = new CustomEventListener(callback);
    element->AddEventListener(event_type, listener, false);

    // Store callback for cleanup
    std::string key = element_id + ":" + event_type;
    g_state.event_listeners[document_path][key].push_back(callback);

    LOG_INFO("Registered event listener: {} on {}.{}", event_type, document_path, element_id);
}

bool ProcessEvent(SDL_Event& event) {
    if (!g_state.initialized || !g_state.main_context) {
        return false;
    }

    return Backend::ProcessEvent(g_state.main_context, event);
}

// ============================================================================
// STYLING HELPERS
// ============================================================================

void SetElementStyle(const std::string& document_path, const std::string& element_id, 
                     const std::string& property, const std::string& value) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        element->SetProperty(property, value);
    }
}

void AddClass(const std::string& document_path, const std::string& element_id, const std::string& class_name) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        element->SetClass(class_name, true);
    }
}

void RemoveClass(const std::string& document_path, const std::string& element_id, const std::string& class_name) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        element->SetClass(class_name, false);
    }
}

bool HasClass(const std::string& document_path, const std::string& element_id, const std::string& class_name) {
    Rml::Element* element = GetElement(document_path, element_id);
    if (element) {
        return element->IsClassSet(class_name);
    }
    return false;
}

// ============================================================================
// DATA BINDING
// ============================================================================

Rml::DataModelConstructor BindDataModel(const std::string& model_name) {
    if (!g_state.initialized || !g_state.main_context) {
        LOG_ERROR("RmlUI not initialized");
        return Rml::DataModelConstructor();
    }

    auto constructor = g_state.main_context->CreateDataModel(model_name);
    if (constructor) {
        LOG_INFO("Data model '{}' created successfully", model_name);
    } else {
        LOG_ERROR("Failed to create data model '{}'", model_name);
    }
    
    return constructor;
}

// ============================================================================
// COMMON UI HELPERS
// ============================================================================

void ShowMessageBox(const std::string& title, const std::string& message, 
                   std::function<void()> on_ok) {
    if (!g_state.initialized) {
        return;
    }

    // Create a simple message box RML on the fly
    std::string rml = R"(
<rml>
<head>
    <title>)" + title + R"(</title>
    <style>
        body {
            width: 400px;
            height: 200px;
            position: absolute;
            left: 50%;
            top: 50%;
            margin-left: -200px;
            margin-top: -100px;
            background-color: #333;
            border: 2px solid #666;
            padding: 20px;
        }
        
        .title {
            font-size: 20px;
            color: #fff;
            margin-bottom: 20px;
        }
        
        .message {
            color: #ccc;
            margin-bottom: 30px;
        }
        
        button {
            width: 100px;
            height: 30px;
            background-color: #555;
            color: #fff;
            border: 1px solid #777;
            cursor: pointer;
        }
        
        button:hover {
            background-color: #666;
        }
    </style>
</head>
<body>
    <div class="title">)" + title + R"(</div>
    <div class="message">)" + message + R"(</div>
    <button id="ok_button">OK</button>
</body>
</rml>
)";

    // Load the message box
    Rml::ElementDocument* doc = g_state.main_context->LoadDocumentFromMemory(rml);
    if (doc) {
        doc->Show();
        
        // Register OK button handler
        if (on_ok) {
            RegisterEventListener("msgbox", "ok_button", "click", [on_ok, doc](Rml::Event&) {
                on_ok();
                doc->Close();
            });
        } else {
            RegisterEventListener("msgbox", "ok_button", "click", [doc](Rml::Event&) {
                doc->Close();
            });
        }
    }
}

void ShowConfirmDialog(const std::string& title, const std::string& message,
                      std::function<void(bool)> callback) {
    if (!g_state.initialized || !callback) {
        return;
    }

    // Create a confirmation dialog RML
    std::string rml = R"(
<rml>
<head>
    <title>)" + title + R"(</title>
    <style>
        body {
            width: 400px;
            height: 200px;
            position: absolute;
            left: 50%;
            top: 50%;
            margin-left: -200px;
            margin-top: -100px;
            background-color: #333;
            border: 2px solid #666;
            padding: 20px;
        }
        
        .title {
            font-size: 20px;
            color: #fff;
            margin-bottom: 20px;
        }
        
        .message {
            color: #ccc;
            margin-bottom: 30px;
        }
        
        .buttons {
            text-align: right;
        }
        
        button {
            width: 100px;
            height: 30px;
            background-color: #555;
            color: #fff;
            border: 1px solid #777;
            cursor: pointer;
            margin-left: 10px;
        }
        
        button:hover {
            background-color: #666;
        }
    </style>
</head>
<body>
    <div class="title">)" + title + R"(</div>
    <div class="message">)" + message + R"(</div>
    <div class="buttons">
        <button id="yes_button">Yes</button>
        <button id="no_button">No</button>
    </div>
</body>
</rml>
)";

    Rml::ElementDocument* doc = g_state.main_context->LoadDocumentFromMemory(rml);
    if (doc) {
        doc->Show();
        
        // Register button handlers
        RegisterEventListener("confirm", "yes_button", "click", [callback, doc](Rml::Event&) {
            callback(true);
            doc->Close();
        });
        
        RegisterEventListener("confirm", "no_button", "click", [callback, doc](Rml::Event&) {
            callback(false);
            doc->Close();
        });
    }
}

// ============================================================================
// DEBUG HELPERS
// ============================================================================

void ShowDebugOverlay(bool show) {
    g_state.debug_overlay_enabled = show;
    // Note: Actual debug visualization would be implemented in the render loop
}

void SetDebugText(const std::string& key, const std::string& value) {
    g_state.debug_values[key] = value;
}

void EnableDebugger(bool enable) {
    if (!g_state.initialized || !g_state.main_context) {
        return;
    }

#ifdef RMLUI_DEBUGGER_ENABLED
    if (enable) {
        Rml::Debugger::Initialise(g_state.main_context);
        LOG_INFO("RmlUI debugger enabled");
    } else {
        Rml::Debugger::Shutdown();
        LOG_INFO("RmlUI debugger disabled");
    }
#else
    if (enable) {
        LOG_WARNING("RmlUI debugger requested but not compiled in");
    }
#endif
}

bool IsInitialized() {
    return g_state.initialized;
}

} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
