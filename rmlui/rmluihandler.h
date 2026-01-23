/*
 * RmlUI Handler - Public API for Luminoveau Engine
 * Provides a simplified wrapper around RmlUi for easy UI integration
 */

#pragma once

#ifdef LUMINOVEAU_WITH_RMLUI

#include <RmlUi/Core.h>
#include <string>
#include <functional>
#include <unordered_map>
#include "utils/vectors.h"
#include "utils/colors.h"

namespace RmlUI {

// ============================================================================
// LIFECYCLE - Initialize and shutdown the UI system
// ============================================================================

/**
 * @brief Initialize RmlUI system (call after Window::InitWindow)
 * Creates the main context and sets up rendering backend
 */
void Init();

/**
 * @brief Shutdown RmlUI system (call before Window::Close)
 * Cleans up all resources and contexts
 */
void Shutdown();

// ============================================================================
// CONTEXT MANAGEMENT
// ============================================================================

/**
 * @brief Load a font face from a file
 * @param filepath Path to the font file (.ttf, .otf, etc.)
 * @param fallback Whether this is a fallback font (default: false)
 * @return True if loaded successfully
 */
bool LoadFontFromFile(const std::string& filepath, bool fallback = false);

/**
 * @brief Load a font face from memory
 * @param data Pointer to font data in memory
 * @param data_length Length of font data in bytes
 * @param family Font family name
 * @param style Font style (Normal, Italic)
 * @param weight Font weight (Normal, Bold, etc.)
 * @param fallback Whether this is a fallback font (default: false)
 * @return True if loaded successfully
 */
bool LoadFontFromMemory(const unsigned char* data, size_t data_length, 
                       const std::string& family, 
                       Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal,
                       Rml::Style::FontWeight weight = Rml::Style::FontWeight::Normal,
                       bool fallback = false);

/**
 * @brief Load the built-in DroidSansMono font
 * Convenience function that loads the embedded default font
 * @return True if loaded successfully
 */
bool LoadDefaultFont();

/**
 * @brief Get the main UI context (created automatically on Init)
 * @return Pointer to the main context
 */
Rml::Context* GetContext();

/**
 * @brief Create an additional context with custom size
 * @param name Unique name for the context
 * @param size Size of the context in pixels
 * @return Pointer to the created context
 */
Rml::Context* CreateContext(const std::string& name, vf2d size);

/**
 * @brief Get a context by name
 * @param name Name of the context
 * @return Pointer to the context, or nullptr if not found
 */
Rml::Context* GetContextByName(const std::string& name);

// ============================================================================
// DOCUMENT MANAGEMENT - AssetHandler-style interface
// ============================================================================

/**
 * @brief Load an RML document from file
 * @param filepath Path to the .rml file
 * @return Pointer to the loaded document, or nullptr on failure
 */
Rml::ElementDocument* LoadDocument(const std::string& filepath);

/**
 * @brief Get a previously loaded document
 * @param filepath Path that was used to load the document
 * @return Pointer to the document, or nullptr if not loaded
 */
Rml::ElementDocument* GetDocument(const std::string& filepath);

/**
 * @brief Show a document (makes it visible)
 * @param filepath Path to the document
 */
void ShowDocument(const std::string& filepath);

/**
 * @brief Hide a document (makes it invisible but keeps it loaded)
 * @param filepath Path to the document
 */
void HideDocument(const std::string& filepath);

/**
 * @brief Toggle document visibility
 * @param filepath Path to the document
 */
void ToggleDocument(const std::string& filepath);

/**
 * @brief Check if a document is currently visible
 * @param filepath Path to the document
 * @return True if visible, false otherwise
 */
bool IsDocumentVisible(const std::string& filepath);

/**
 * @brief Unload a document and free its resources
 * @param filepath Path to the document
 */
void UnloadDocument(const std::string& filepath);

/**
 * @brief Close and unload a document
 * @param filepath Path to the document
 */
void CloseDocument(const std::string& filepath);

// ============================================================================
// RENDERING - Frame cycle integration
// ============================================================================

/**
 * @brief Update and render all UI contexts
 * Call this after game rendering, before Window::EndFrame
 */
void Render();

/**
 * @brief Update UI without rendering (for logic updates)
 */
void Update();

// ============================================================================
// ELEMENT MANIPULATION - Direct element access helpers
// ============================================================================

/**
 * @brief Set text content of an element by ID
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param text New text content
 */
void SetElementText(const std::string& document_path, const std::string& element_id, const std::string& text);

/**
 * @brief Set value of an input element by ID
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param value New value
 */
void SetElementValue(const std::string& document_path, const std::string& element_id, const std::string& value);

/**
 * @brief Get text content of an element by ID
 * @param document_path Path to the document
 * @param element_id Element ID
 * @return Text content, or empty string if not found
 */
std::string GetElementText(const std::string& document_path, const std::string& element_id);

/**
 * @brief Get value of an input element by ID
 * @param document_path Path to the document
 * @param element_id Element ID
 * @return Element value, or empty string if not found
 */
std::string GetElementValue(const std::string& document_path, const std::string& element_id);

/**
 * @brief Get an element by ID from a document
 * @param document_path Path to the document
 * @param element_id Element ID
 * @return Pointer to element, or nullptr if not found
 */
Rml::Element* GetElement(const std::string& document_path, const std::string& element_id);

// ============================================================================
// EVENT HANDLING
// ============================================================================

/// Event callback function type
using EventCallback = std::function<void(Rml::Event&)>;

/**
 * @brief Register an event listener on an element
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param event_type Event type (e.g., "click", "change", "submit")
 * @param callback Function to call when event fires
 */
void RegisterEventListener(const std::string& document_path, const std::string& element_id, 
                          const std::string& event_type, EventCallback callback);

/**
 * @brief Process an SDL event (integrates with Input system)
 * @param event SDL event to process
 * @return True if event was consumed by UI
 */
bool ProcessEvent(SDL_Event& event);

// ============================================================================
// STYLING HELPERS - Programmatic CSS manipulation
// ============================================================================

/**
 * @brief Set an inline style property on an element
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param property CSS property name
 * @param value CSS property value
 */
void SetElementStyle(const std::string& document_path, const std::string& element_id, 
                     const std::string& property, const std::string& value);

/**
 * @brief Add a CSS class to an element
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param class_name CSS class name to add
 */
void AddClass(const std::string& document_path, const std::string& element_id, const std::string& class_name);

/**
 * @brief Remove a CSS class from an element
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param class_name CSS class name to remove
 */
void RemoveClass(const std::string& document_path, const std::string& element_id, const std::string& class_name);

/**
 * @brief Check if an element has a CSS class
 * @param document_path Path to the document
 * @param element_id Element ID
 * @param class_name CSS class name to check
 * @return True if element has the class
 */
bool HasClass(const std::string& document_path, const std::string& element_id, const std::string& class_name);

// ============================================================================
// DATA BINDING - Simplified data model API
// ============================================================================

/**
 * @brief Create and bind a data model to a context
 * @param model_name Name of the data model
 * @return DataModelConstructor to configure the model, or empty constructor on failure
 */
Rml::DataModelConstructor BindDataModel(const std::string& model_name);

// ============================================================================
// COMMON UI HELPERS - High-level convenience functions
// ============================================================================

/**
 * @brief Show a simple message box
 * @param title Title of the message box
 * @param message Message content
 * @param on_ok Optional callback when OK is clicked
 */
void ShowMessageBox(const std::string& title, const std::string& message, 
                   std::function<void()> on_ok = nullptr);

/**
 * @brief Show a confirmation dialog with Yes/No buttons
 * @param title Title of the dialog
 * @param message Message content
 * @param callback Callback with true for Yes, false for No
 */
void ShowConfirmDialog(const std::string& title, const std::string& message,
                      std::function<void(bool)> callback);

// ============================================================================
// DEBUG HELPERS
// ============================================================================

/**
 * @brief Enable/disable debug overlay showing element outlines
 * @param show True to show debug overlay
 */
void ShowDebugOverlay(bool show = true);

/**
 * @brief Set a debug text value in the debug overlay
 * @param key Key for the debug value
 * @param value String value to display
 */
void SetDebugText(const std::string& key, const std::string& value);

/**
 * @brief Enable/disable the visual debugger
 * @param enable True to enable debugger
 */
void EnableDebugger(bool enable = true);

/**
 * @brief Check if RmlUI is initialized
 * @return True if initialized
 */
bool IsInitialized();

} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
