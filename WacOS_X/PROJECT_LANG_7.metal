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
// For: WacOS X
// About:
// I chose Metal as the seventh project language for this project (WacOS X) as Metal is an Apple language I want to emulate and resemble here in the deep stages (10.12-10.15) of this project. It is being used in specific APIs within the later subsystems. I decided to resemble all Apple languages through their usage on Wac OS X subsystem projects. It is getting its own project language file, starting here.

/* File info
* File type: Metal source file (*.metal)
* File version: 1 (2022, Wednesday, June 1st at 6:28 pm PST)
* Line count (including blank lines and compiler line): 24
*/
// End of script
