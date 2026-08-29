# Shader fixtures

`multi-entry.hlsl` contains vertex, fragment, and compute entry points in one
strict-HLSL translation unit. The native feasibility checks use it to exercise
the three v0.1 output targets and reflection. Virtual includes and diagnostic
fixtures remain deferred until the C ABI implementation.
