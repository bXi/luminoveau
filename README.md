# Luminoveau

Luminoveau is a C++ library designed to simplify game development and creative programming.

## About

Luminoveau provides a set of tools and utilities to help developers build immersive experiences, from 2D games to interactive art installations.

## Key Features

- **Performance:** Designed for speed and ease of use, ensuring smooth gameplay and real-time interactions.
- **Ease of Use:** Simple and intuitive APIs make it easy to get started, while comprehensive documentation guides you through the process.

## Getting Started

1. **Clone the Repository:**
    ```bash
    git clone https://github.com/bXi/luminoveau.git
    ```

2. **Integrate with CMake:**
    Integrate Luminoveau directly into your C++ project using CMake. Here's an example of how to do this in your CMakeLists.txt file:
    ```cmake
    # We require C++17 as a minimum
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    
    # Add luminoveau as a subdirectory
    add_subdirectory(luminoveau)

    # Link your project with luminoveau
    target_link_libraries(your_project luminoveau)

    # Include Luminoveau headers
    target_include_directories(your_project PRIVATE luminoveau)
    ```

3. **Build Your Project:**
    Use CMake to build your project as usual. Luminoveau will be automatically included and linked.
    ```bash
    cd your_project
    mkdir build && cd build
    cmake ..
    make
    ```

## Example program
  This is the bare minimum to get a window going.
  ```cpp
  #include "luminoveau.h"
  
  int main()
  {
    Window::InitWindow("Test");
  
    while (!Window::ShouldQuit()) {
      Window::StartFrame();
  
      Render2D::DrawRectangle({ 10, 10 }, { 50, 50 }, WHITE);
  
      Window::EndFrame();
    }
  }
  ```

## Documentation

Documentation and examples are available in the [Wiki](https://docs.luminoveau.net).

## License

Luminoveau is released under the MIT License. See the [LICENSE](./LICENSE.md) file for details.

## Contact

For questions, feedback, or support, feel free to [contact us](mailto:info@luminoveau.net). Or find us on our [discord server](https://discord.gg/gE5mXdCZrn).

## Special Thanks

We want to express our appreciation to the following individuals and projects for their significant contributions to the game development community:

- **[Javidx9](https://github.com/OneLoneCoder):** Thank you for your informative tutorials and amazing content on your YouTube channel. You made me pick up C++ and learn a new way of shooting myself in the foot. Your dedication to sharing knowledge has inspired many developers.

- **[raylib](https://www.raylib.com/):** We are grateful for raylib, a simple and effective C library for game development. Its commitment to providing accessible tools has empowered developers of all levels.

- **[SDL](https://www.libsdl.org/):** Thanks to the SDL (Simple DirectMedia Layer) project for its cross-platform multimedia development library. Its reliability has been invaluable in many projects.

- **[ImGui](https://github.com/ocornut/imgui):** We appreciate ImGui for its intuitive and customizable GUI toolkit, simplifying UI creation.

- **[miniaudio](https://github.com/mackron/miniaudio):** Thank you for miniaudio, a lightweight audio playback and capture library. Its simplicity and ease of integration have been invaluable in audio projects.

- **[stb](https://github.com/nothings/stb):** We are thankful for the STB (Sean Barrett) libraries, offering efficient and reliable single-file solutions for various tasks.

- **[SDL2_gfxPrimitives.c](https://github.com/ferzkopp/SDL_gfx):** Special mention to SDL2_gfxPrimitives.c for extending SDL2's graphics capabilities.

We acknowledge and appreciate the contributions of these individuals and projects to the game development community.
