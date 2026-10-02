#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// CUDA error checking
// ============================================================

#define CUDA_CHECK(call)                                      \
do {                                                          \
    cudaError_t error = call;                                 \
    if (error != cudaSuccess) {                               \
        std::cerr << "CUDA error: "                           \
                  << cudaGetErrorString(error)                \
                  << " at line " << __LINE__ << std::endl;    \
        return EXIT_FAILURE;                                  \
    }                                                         \
} while (0)

// ============================================================
// Load a PPM image
// ============================================================
bool loadPPM(
    const std::string& filename,
    std::vector<unsigned char>& image,
    int& width,
    int& height
)
{
    std::ifstream file(filename, std::ios::binary); // input file stream to read binary data

    if (!file)
    {
        std::cerr << "Could not open input file: "
                  << filename << std::endl;

        return false;
    }

    std::string format; // PPM format identifier (e.g., "P6")
    file >> format;

    if (format != "P6")
    {
        std::cerr << "Only P6 PPM images are supported."
                  << std::endl;

        return false;
    }

    file >> width >> height; // Read image dimensions

    int maxValue;
    file >> maxValue;

    if (maxValue != 255)
    {
        std::cerr << "Unsupported colour depth."
                  << std::endl;

        return false;
    }

    // Move past whitespace after the header
    file.get();

    image.resize(
        width * height * 3
    );

    file.read(
        reinterpret_cast<char*>(image.data()), // Read pixel data into the image vector
        image.size()
    );

    return true;
}

// ============================================================
// Save a PPM image
// ============================================================

bool savePPM(
    const std::string& filename,
    const std::vector<unsigned char>& image,
    int width,
    int height
)
{
    std::ofstream file(
        filename,
        std::ios::binary
    );

    if (!file)
    {
        std::cerr << "Could not create output file: "
                  << filename << std::endl;

        return false;
    }

    file << "P6\n";
    file << width << " " << height << "\n";
    file << "255\n";

    file.write(
        reinterpret_cast<const char*>(image.data()),
        image.size()
    );

    return true;
}

// ============================================================
// CUDA rotation kernel
//
// One CUDA thread = one OUTPUT pixel
// ============================================================

__global__ void rotateKernel(
    const unsigned char* input,
    unsigned char* output,

    int inputWidth,
    int inputHeight,

    int outputWidth,
    int outputHeight,

    float cosTheta,
    float sinTheta
)
{
    // --------------------------------------------------------
    // Calculate this thread's output pixel coordinate
    // --------------------------------------------------------

    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    // --------------------------------------------------------
    // Make sure this thread corresponds to a real pixel
    // --------------------------------------------------------

    if (x >= outputWidth || y >= outputHeight)
    {
        return;
    }

    // --------------------------------------------------------
    // Centre coordinates
    // --------------------------------------------------------

    float inputCentreX =
        (inputWidth - 1) / 2.0f;

    float inputCentreY =
        (inputHeight - 1) / 2.0f;

    float outputCentreX =
        (outputWidth - 1) / 2.0f;

    float outputCentreY =
        (outputHeight - 1) / 2.0f;

    // --------------------------------------------------------
    // Move output pixel relative to output centre
    // --------------------------------------------------------

    float centredX =
        x - outputCentreX;

    float centredY =
        y - outputCentreY;

    // --------------------------------------------------------
    // Inverse rotation
    // --------------------------------------------------------

    float sourceX =
        centredX * cosTheta +
        centredY * sinTheta;

    float sourceY =
        -centredX * sinTheta +
        centredY * cosTheta;

    // --------------------------------------------------------
    // Move back into input image coordinates
    // --------------------------------------------------------

    sourceX += inputCentreX;
    sourceY += inputCentreY;

    // --------------------------------------------------------
    // Nearest-neighbour sampling
    // --------------------------------------------------------

    int srcX = (int)roundf(sourceX);
    int srcY = (int)roundf(sourceY);

    // --------------------------------------------------------
    // Calculate output pixel location
    // RGB = 3 bytes per pixel
    // --------------------------------------------------------

    int outputIndex =
        (y * outputWidth + x) * 3;

    // --------------------------------------------------------
    // If source coordinate is outside image,
    // make output pixel white
    // --------------------------------------------------------

    if (
        srcX < 0 ||
        srcX >= inputWidth ||
        srcY < 0 ||
        srcY >= inputHeight
    )
    {
        output[outputIndex]     = 255;
        output[outputIndex + 1] = 255;
        output[outputIndex + 2] = 255;

        return;
    }

    // --------------------------------------------------------
    // Calculate source pixel location
    // --------------------------------------------------------

    int sourceIndex =
        (srcY * inputWidth + srcX) * 3;

    // --------------------------------------------------------
    // Copy RGB values
    // --------------------------------------------------------

    output[outputIndex] =
        input[sourceIndex];

    output[outputIndex + 1] =
        input[sourceIndex + 1];

    output[outputIndex + 2] =
        input[sourceIndex + 2];
}


// ============================================================
// Main program
// ============================================================

int main()
{
    // --------------------------------------------------------
    // File names
    // --------------------------------------------------------

    const std::string inputFilename =
        "input.ppm";

    const std::string outputFilename =
        "rotated_gpu.ppm";

    // --------------------------------------------------------
    // Change this to test different angles
    // --------------------------------------------------------

    const float angle = 90.0f;

    std::cout << "RUNNING CUDA ROTATION" << std::endl;
    std::cout << "Angle: "
              << angle
              << " degrees"
              << std::endl;


    // ========================================================
    // Load image
    // ========================================================

    std::vector<unsigned char> inputImage;

    int inputWidth;
    int inputHeight;

    if (!loadPPM(
        inputFilename,
        inputImage,
        inputWidth,
        inputHeight
    ))
    {
        return EXIT_FAILURE;
    }

    std::cout << "Input size: "
              << inputWidth
              << " x "
              << inputHeight
              << std::endl;


    // ========================================================
    // Calculate rotation values
    // ========================================================

    float theta =
        angle * 3.14159265358979323846f / 180.0f;

    float cosTheta = cosf(theta);
    float sinTheta = sinf(theta);


    // ========================================================
    // Calculate output image dimensions
    // ========================================================

    int outputWidth = (int)ceilf(
        fabsf(inputWidth * cosTheta) +
        fabsf(inputHeight * sinTheta)
    );

    int outputHeight = (int)ceilf(
        fabsf(inputWidth * sinTheta) +
        fabsf(inputHeight * cosTheta)
    );

    std::cout << "Output size: "
              << outputWidth
              << " x "
              << outputHeight
              << std::endl;


    // ========================================================
    // Calculate memory requirements
    // ========================================================

    size_t inputSize =
        inputWidth *
        inputHeight *
        3 *
        sizeof(unsigned char);

    size_t outputSize =
        outputWidth *
        outputHeight *
        3 *
        sizeof(unsigned char);

    std::vector<unsigned char> outputImage(
        outputWidth *
        outputHeight *
        3
    );


    // ========================================================
    // Allocate GPU memory
    // ========================================================

    unsigned char* d_input = nullptr;
    unsigned char* d_output = nullptr;

    CUDA_CHECK(
        cudaMalloc(
            &d_input,
            inputSize
        )
    );

    CUDA_CHECK(
        cudaMalloc(
            &d_output,
            outputSize
        )
    );


    // ========================================================
    // Copy image from CPU memory to GPU memory
    // ========================================================

    CUDA_CHECK(
        cudaMemcpy(
            d_input,
            inputImage.data(),
            inputSize,
            cudaMemcpyHostToDevice
        )
    );


    // ========================================================
    // Configure CUDA threads
    // ========================================================

    dim3 block(
        16,
        16
    );

    dim3 grid(
        (outputWidth + block.x - 1) / block.x, // Calculate number of blocks in x dimension
        (outputHeight + block.y - 1) / block.y // Calculate number of blocks in y dimension
    );

    std::cout << "Block size: "
              << block.x
              << " x "
              << block.y
              << std::endl;

    std::cout << "Grid size: "
              << grid.x
              << " x "
              << grid.y
              << std::endl;


    // ========================================================
    // Launch GPU kernel
    // ========================================================

    rotateKernel<<<grid, block>>>(
        d_input,
        d_output,

        inputWidth,
        inputHeight,

        outputWidth,
        outputHeight,

        cosTheta,
        sinTheta
    );


    // Check whether kernel launch succeeded
    CUDA_CHECK(
        cudaGetLastError()
    );


    // Wait for GPU to finish
    CUDA_CHECK(
        cudaDeviceSynchronize()
    );


    // ========================================================
    // Copy result from GPU back to CPU
    // ========================================================

    CUDA_CHECK(
        cudaMemcpy(
            outputImage.data(),
            d_output,
            outputSize,
            cudaMemcpyDeviceToHost
        )
    );


    // ========================================================
    // Save output
    // ========================================================

    if (!savePPM(
        outputFilename,
        outputImage,
        outputWidth,
        outputHeight
    ))
    {
        return EXIT_FAILURE;
    }

    std::cout << "Saved: "
              << outputFilename
              << std::endl;

    std::cout << "CUDA rotation complete!"
              << std::endl;


    // ========================================================
    // Free GPU memory
    // ========================================================

    CUDA_CHECK(
        cudaFree(d_input)
    );

    CUDA_CHECK(
        cudaFree(d_output)
    );

    return EXIT_SUCCESS;
}