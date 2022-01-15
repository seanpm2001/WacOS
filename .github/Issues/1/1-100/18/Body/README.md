***

### [Review needed] Swap file creator

I created a new component of the system today, which is a SWAP file creator. It isn't functional yet, but I have already run into a design problem:

Modern SWAP files are very large. For reference, my laptops SWAP file is 2 gigabytes (2 trillion bytes) the SWAP file maker I made is designed to take up less memory in its source code, but build a SWAP file byte by byte, running a command for every blank byte added to the SWAP file located at [SWAPFILE.bin](https://github.com/seanpm2001/WacOS/blob/WacOS-dev/SWAPFILE.bin) (which I will change to .swp in the future) I can't just upload a SWAP file to GitHub, as 1. It would be too large and 2. Not every SWAP file for every installation is the same size.

This is a fragment of the 3rd variation of the C code for reference. It is not functional yet and is mostly pseudocode (you might need to view the full file to get all the context)

```c
// Create a custom size SWAP file
int customSWAP(void) {
	scanf.csw("Please specify the size of the SWAP file in bytes. Make sure the SWAP file is less than 1/4 of your RAM size\n");
	if (csw / 4 > ramSize) {
		printf("The specified size is too large\n");
		break;
	} else {
		x = int(0)
		while (x != csw) {
			file.append(0) to file("/SWAPFILE.bin");
			x == x + 1;
			if response == true {
				response == false;
			} else if response == false {
				response == true;
			}
			if (x == csw) {
				printf("A " + int(csw) + " byte SWAP file has been created!\n");
				break;
			}
	}
}
```

The full source code can be viewed [here](https://github.com/seanpm2001/WacOS/blob/WacOS-dev/VIRTUAL-MEMORY.c) or [here](https://github.com/seanpm2001/WacOS/blob/WacOS-dev/SWAPFILE_CREATOR.c)

I have been told by family members that this sounds like a virus. I don't know enough about it to agree, but I feel like doing this could damage/destroy a storage device easily. I wonder if there is a better way of doing this. Does anyone know?

***

**File version:** `1 (2022, Friday, January 14th at 7:37 pm)`

***
