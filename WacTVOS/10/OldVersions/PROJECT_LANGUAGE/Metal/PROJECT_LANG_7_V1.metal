// Start of script
#include <metal_stdlib>
using namespace metal;

// Sample memory function
kernel void compute(const device float *inVector [[ buffer(0) ]],
                    device float *outVector [[ buffer(1) ]],
                    uint id [[ thread_position_in_grid ]])
{
    outVector[id] = 1.0 / (1.0 + exp(-inVector[id]));
}

// Project language 7
// For: WacOS/WacTVOS 10
// About:
// I chose Metal as the seventh project language for this project (WacOS/WacTVOS/10) as Metal is an Apple language I want to emulate and resemble it here for cross compatibility and software diversity reasons. It is being used in specific APIs within the system, and may replace GLSL/HLSL in some areas. I decided to resemble all Apple languages through their usage on WacTVOS subsystem projects, and this is the 7th (and currently final) official language in use here. It is getting its own project language file, starting here.

/* File info
* File type: Metal source file (*.metal)
* File version: 1 (2022, Monday, June 20th at 3:10 pm PST)
* Line count (including blank lines and compiler line): 24
*/
// End of script
