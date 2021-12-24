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

// I chose Metal as the thirty-second project language for this project (Seanpm2001/WacOS) as Metal is an Apple language I want to emulate and resemble here. It will be used alongside other shader languages. I also wanted a project language file to represent this.

/* File info
* File type: Metal source file (*.metal)
* File version: 1 (2021, Thursday, December 23rd at 8:54 pm)
* Line count (including blank lines and compiler line): 21
*/
// End of script
