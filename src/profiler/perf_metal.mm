// macOS GPU name via Metal. Compiled only on Apple platforms (see Sources.cmake).
#import <Metal/Metal.h>
#include <string>

extern "C" const char *lumi_metal_gpu_name() {
    static std::string name;
    if (name.empty()) {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev && dev.name) name = std::string([dev.name UTF8String]);
        else                 name = "Unknown GPU";
    }
    return name.c_str();
}
