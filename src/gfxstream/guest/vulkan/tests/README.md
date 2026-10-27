# Push descriptor integration test

Build `gfxstream_push_descriptor_test` in an Android tree and run it on a device
that exposes `VK_KHR_push_descriptor`. It returns nonzero on failure and compares
GPU output with expected values. The ARM64 binary can also be run through
`berberis_program_runner_arm64` on an x86_64 emulator with ARM64 translation.

Coverage: ordinary and partial pushes, core and KHR template creation, padded and
unaligned template data, zero stride, buffer/image/texel descriptors, ignored
fields, immutable samplers, destroyed descriptor set layouts, and command buffer
reset, secondary command buffers, and ordinary descriptor template regression. Requires Vulkan 1.1 and `VK_KHR_descriptor_update_template`.

`push_descriptor_spv.h` embeds `push_descriptor.comp`, compiled with
`glslc --target-env=vulkan1.1`. The generated words are little-endian SPIR-V.
