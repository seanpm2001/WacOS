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

// I chose Metal as the fifth project language for this project (WacOS/APIs) as Metal is an Apple language I want to emulate and resemble here. It is being used in specific APIs. I decided to resemble all Apple languages through API usage. It is getting its own project language file, starting here.

/* File info
* File type: Metal source file (*.metal)
* File version: 1 (2022, Tuesday, May 17th at 2:02 pm PST)
* Line count (including blank lines and compiler line): 21
*/
// End of script
