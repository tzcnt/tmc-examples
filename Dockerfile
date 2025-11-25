# Stage 1: Build Stage
FROM silkeh/clang:latest AS build

# Install build dependencies if needed (e.g., CMake)
RUN apt-get update && apt-get install -y cmake ninja-build git libhwloc-dev hwloc

# Copy source code and CMakeLists.txt
COPY ./cmake ./cmake
COPY ./examples ./examples
COPY ./tests ./tests
COPY ./submodules ./submodules
COPY ./CMakeLists.txt .
COPY ./CMakePresets.json .

# Build the application
RUN cmake --preset clang-linux-release .
RUN cmake --build ./build/clang-linux-release --parallel 16 --target fib
CMD ["./fib", "30"]


# # Stage 2: Runtime Stage
# FROM debian:bookworm

# # # Install runtime dependencies if needed (e.g., libstdc++)
# RUN apt-get update && apt-get install -y libhwloc-dev

# # Set working directory
# WORKDIR /app

# # Copy the compiled binary from the build stage
# COPY --from=build /build/clang-linux-release/fib .

# # Define the command to run the application
# CMD ["./fib", "30"]
